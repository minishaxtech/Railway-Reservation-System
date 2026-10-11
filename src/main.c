/*
 * Railway Reservation System
 * --------------------------
 * A console application written in pure C (C99, standard library only).
 *
 * Features
 *   1. Ticket booking
 *   2. Seat availability (seat matrix)
 *   3. Passenger information (by PNR, or whole train)
 *   4. Ticket cancellation (with refund)
 *
 * Bookings are saved to a plain text file (bookings.txt) so they survive
 * between runs.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
/*
 * Browser build (WebAssembly). The C logic is unchanged; only the lowest
 * layer is swapped: reading a line asks the web page for it, and the
 * bookings file is mirrored into the browser's localStorage.
 */
#include <emscripten.h>

EM_ASYNC_JS(int, web_get_line, (const char *prompt, char *buf, int size), {
    const line = await Module.getLine(UTF8ToString(prompt));
    if (line === null) return 0;
    stringToUTF8(line, buf, size);
    return 1;
});

EM_JS(void, web_restore, (void), {
    const data = Module.storageGet();
    if (data) FS.writeFile('/bookings.txt', data);
});

EM_JS(void, web_persist, (void), {
    Module.storageSet(FS.readFile('/bookings.txt', { encoding: 'utf8' }));
});
#endif

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

#define MAX_TRAINS        5
#define SEATS_PER_TRAIN   40
#define SEATS_PER_ROW     8
#define MAX_PASSENGERS    (MAX_TRAINS * SEATS_PER_TRAIN)
#define NAME_LEN          50
#define DATA_FILE         "bookings.txt"
#define PNR_BASE          1000000000LL
#define PNR_MAX           9999999999LL
#define REFUND_PERCENT    80

/* ------------------------------------------------------------------ */
/* Data structures                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    int  number;
    char name[32];
    char from[24];
    char to[24];
    char departs[8];
    int  fare;                  /* in rupees */
} Train;

typedef struct {
    long long pnr;
    int       train;            /* index into trains[] */
    int       seat;             /* 1..SEATS_PER_TRAIN */
    int       age;
    char      gender;           /* 'M', 'F' or 'O' */
    char      name[NAME_LEN];
} Passenger;

/* Sample data - change freely. */
static const Train trains[MAX_TRAINS] = {
    {12951, "Mumbai Rajdhani",   "Mumbai Central", "New Delhi",  "16:35", 2450},
    {12301, "Howrah Rajdhani",   "Howrah",         "New Delhi",  "16:55", 2800},
    {12627, "Karnataka Express", "Bengaluru",      "New Delhi",  "19:20", 1650},
    {12841, "Coromandel Express","Howrah",         "Chennai",    "14:50", 1350},
    {12009, "Shatabdi Express",  "Mumbai Central", "Ahmedabad",  "06:25",  900}
};

static Passenger bookings[MAX_PASSENGERS];
static int       booking_count = 0;
static long long next_pnr      = PNR_BASE + 1;

/* ------------------------------------------------------------------ */
/* Input helpers (every read is a full line, so bad input never        */
/* leaves junk in the buffer)                                          */
/* ------------------------------------------------------------------ */

/* Reads one line into buf (without the newline). Returns 0 on EOF. */
static int read_line(const char *prompt, char *buf, size_t size)
{
#ifdef __EMSCRIPTEN__
    return web_get_line(prompt, buf, (int)size);
#else
    size_t len;

    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, (int)size, stdin) == NULL)
        return 0;

    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    } else {
        int c;                   /* line was longer than the buffer */
        while ((c = getchar()) != '\n' && c != EOF)
            ;
    }
    return 1;
#endif
}

static void trim(char *s)
{
    size_t n = strlen(s), start = 0;

    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    while (s[start] != '\0' && isspace((unsigned char)s[start]))
        start++;
    if (start > 0)
        memmove(s, s + start, strlen(s + start) + 1);
}

/* Reads an integer in [min, max]; keeps asking until valid. 0 on EOF. */
static int read_int(const char *prompt, int min, int max, int *out)
{
    char buf[64];

    for (;;) {
        char *end;
        long  v;

        if (!read_line(prompt, buf, sizeof buf))
            return 0;
        v = strtol(buf, &end, 10);
        while (isspace((unsigned char)*end))
            end++;
        if (end == buf || *end != '\0' || v < min || v > max) {
            printf("  Please enter a number between %d and %d.\n", min, max);
            continue;
        }
        *out = (int)v;
        return 1;
    }
}

static int read_pnr(const char *prompt, long long *out)
{
    char buf[64];

    for (;;) {
        char     *end;
        long long v;

        if (!read_line(prompt, buf, sizeof buf))
            return 0;
        v = strtoll(buf, &end, 10);
        while (isspace((unsigned char)*end))
            end++;
        if (end == buf || *end != '\0' || v < 1 || v > PNR_MAX) {
            printf("  Please enter a valid numeric PNR.\n");
            continue;
        }
        *out = v;
        return 1;
    }
}

/* y/n question. EOF counts as "no". */
static int ask_yes_no(const char *prompt)
{
    char buf[16];

    for (;;) {
        if (!read_line(prompt, buf, sizeof buf))
            return 0;
        trim(buf);
        if (buf[0] != '\0' && buf[1] == '\0') {
            if (buf[0] == 'y' || buf[0] == 'Y') return 1;
            if (buf[0] == 'n' || buf[0] == 'N') return 0;
        }
        printf("  Please answer y or n.\n");
    }
}

static int valid_name(const char *s)
{
    size_t len = strlen(s);
    int    has_alpha = 0;

    if (len == 0 || len >= NAME_LEN)
        return 0;
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalpha(c))
            has_alpha = 1;
        else if (c != ' ' && c != '.' && c != '-' && c != '\'')
            return 0;
    }
    return has_alpha;
}

static int read_gender(char *out)
{
    char buf[16];

    for (;;) {
        if (!read_line("  Gender (M/F/O): ", buf, sizeof buf))
            return 0;
        trim(buf);
        if (buf[0] != '\0' && buf[1] == '\0') {
            char g = (char)toupper((unsigned char)buf[0]);
            if (g == 'M' || g == 'F' || g == 'O') {
                *out = g;
                return 1;
            }
        }
        printf("  Please enter M, F or O.\n");
    }
}

static const char *gender_text(char g)
{
    return g == 'M' ? "Male" : g == 'F' ? "Female" : "Other";
}

/* ------------------------------------------------------------------ */
/* Lookups                                                             */
/* ------------------------------------------------------------------ */

static int find_by_seat(int train, int seat)
{
    for (int i = 0; i < booking_count; i++)
        if (bookings[i].train == train && bookings[i].seat == seat)
            return i;
    return -1;
}

static int find_by_pnr(long long pnr)
{
    for (int i = 0; i < booking_count; i++)
        if (bookings[i].pnr == pnr)
            return i;
    return -1;
}

static int count_booked(int train)
{
    int n = 0;
    for (int i = 0; i < booking_count; i++)
        if (bookings[i].train == train)
            n++;
    return n;
}

static int first_free_seat(int train)
{
    for (int s = 1; s <= SEATS_PER_TRAIN; s++)
        if (find_by_seat(train, s) < 0)
            return s;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Persistence                                                         */
/*                                                                     */
/* File format (text, one record per line):                            */
/*   NEXT_PNR <n>                                                      */
/*   <pnr> <train-index> <seat> <age> <gender> <name...>               */
/* ------------------------------------------------------------------ */

static int save_bookings(void)
{
    FILE *fp = fopen(DATA_FILE, "w");

    if (fp == NULL)
        return 0;
    fprintf(fp, "NEXT_PNR %lld\n", next_pnr);
    for (int i = 0; i < booking_count; i++) {
        const Passenger *p = &bookings[i];
        fprintf(fp, "%lld %d %d %d %c %s\n",
                p->pnr, p->train, p->seat, p->age, p->gender, p->name);
    }
    if (fclose(fp) != 0)
        return 0;
#ifdef __EMSCRIPTEN__
    web_persist();
#endif
    return 1;
}

static void load_bookings(void)
{
    FILE      *fp = fopen(DATA_FILE, "r");
    char       line[256];
    long long  max_pnr = PNR_BASE;

    booking_count = 0;
    next_pnr = PNR_BASE + 1;
    if (fp == NULL)
        return;                         /* first run: nothing saved yet */

    while (fgets(line, sizeof line, fp) != NULL) {
        long long pnr, saved_next;
        int       train, seat, age, used = 0;
        char      gender;
        char     *name;

        if (sscanf(line, "NEXT_PNR %lld", &saved_next) == 1) {
            if (saved_next > next_pnr)
                next_pnr = saved_next;
            continue;
        }
        if (sscanf(line, "%lld %d %d %d %c %n",
                   &pnr, &train, &seat, &age, &gender, &used) < 5 || used == 0) {
            fprintf(stderr, "Warning: skipped unreadable line in %s\n", DATA_FILE);
            continue;
        }
        name = line + used;
        trim(name);

        if (train < 0 || train >= MAX_TRAINS ||
            seat < 1 || seat > SEATS_PER_TRAIN ||
            age < 1 || age > 120 ||
            (gender != 'M' && gender != 'F' && gender != 'O') ||
            !valid_name(name) ||
            find_by_seat(train, seat) >= 0 ||
            find_by_pnr(pnr) >= 0 ||
            booking_count >= MAX_PASSENGERS) {
            fprintf(stderr, "Warning: skipped invalid record in %s\n", DATA_FILE);
            continue;
        }

        bookings[booking_count].pnr    = pnr;
        bookings[booking_count].train  = train;
        bookings[booking_count].seat   = seat;
        bookings[booking_count].age    = age;
        bookings[booking_count].gender = gender;
        snprintf(bookings[booking_count].name, NAME_LEN, "%s", name);
        if (pnr > max_pnr)
            max_pnr = pnr;
        booking_count++;
    }
    fclose(fp);

    /* Never hand out a PNR that is already in use. */
    if (next_pnr <= max_pnr)
        next_pnr = max_pnr + 1;
}

/* ------------------------------------------------------------------ */
/* Display helpers                                                     */
/* ------------------------------------------------------------------ */

static void print_trains(void)
{
    printf("\n  No  Train  Name                Route                          Dep    Fare\n");
    printf("  --  -----  ------------------  -----------------------------  -----  -------\n");
    for (int i = 0; i < MAX_TRAINS; i++) {
        char route[64];
        snprintf(route, sizeof route, "%s -> %s", trains[i].from, trains[i].to);
        printf("  %-2d  %-5d  %-18s  %-29s  %-5s  Rs.%d\n",
               i + 1, trains[i].number, trains[i].name, route,
               trains[i].departs, trains[i].fare);
    }
}

/* Asks the user to pick a train. Returns index, or -1 on EOF. */
static int select_train(void)
{
    char prompt[64];
    int  choice;

    print_trains();
    snprintf(prompt, sizeof prompt, "\n  Select train (1-%d): ", MAX_TRAINS);
    if (!read_int(prompt, 1, MAX_TRAINS, &choice))
        return -1;
    return choice - 1;
}

static void show_seat_matrix(int t)
{
    int free_seats = 0;

    printf("\n  Train %d - %s  (%s -> %s)\n",
           trains[t].number, trains[t].name, trains[t].from, trains[t].to);
    printf("  Legend: [nn] available   [XX] booked\n\n");

    for (int s = 1; s <= SEATS_PER_TRAIN; s++) {
        if ((s - 1) % SEATS_PER_ROW == 0)
            printf("  ");
        if (find_by_seat(t, s) >= 0) {
            printf("[XX] ");
        } else {
            printf("[%2d] ", s);
            free_seats++;
        }
        if (s % SEATS_PER_ROW == 0)
            printf("\n");
    }
    printf("\n  Available: %d / %d\n", free_seats, SEATS_PER_TRAIN);
}

static void print_ticket(const Passenger *p, const char *status)
{
    const Train *tr = &trains[p->train];

    printf("\n  +------------------------------------------+\n");
    printf("  |  E-TICKET - %-28s |\n", status);
    printf("  +------------------------------------------+\n");
    printf("   PNR          : %lld\n", p->pnr);
    printf("   Train        : %d %s\n", tr->number, tr->name);
    printf("   Route        : %s -> %s\n", tr->from, tr->to);
    printf("   Departure    : %s\n", tr->departs);
    printf("   Passenger    : %s\n", p->name);
    printf("   Age / Gender : %d / %s\n", p->age, gender_text(p->gender));
    printf("   Seat No.     : %d\n", p->seat);
    printf("   Fare         : Rs. %d\n", tr->fare);
    printf("  +------------------------------------------+\n");
}

/* ------------------------------------------------------------------ */
/* Features                                                            */
/* ------------------------------------------------------------------ */

static void book_ticket(void)
{
    Passenger p;
    char      name[128];
    int       t, seat;

    printf("\n=== BOOK A TICKET ===\n");
    t = select_train();
    if (t < 0)
        return;

    if (count_booked(t) >= SEATS_PER_TRAIN) {
        printf("\n  Sorry, this train is fully booked.\n");
        return;
    }

    show_seat_matrix(t);
    for (;;) {
        if (!read_int("\n  Seat number (0 = auto-assign): ", 0, SEATS_PER_TRAIN, &seat))
            return;
        if (seat == 0) {
            seat = first_free_seat(t);
            break;
        }
        if (find_by_seat(t, seat) >= 0) {
            printf("  Seat %d is already booked. Choose another seat.\n", seat);
            continue;
        }
        break;
    }

    for (;;) {
        if (!read_line("  Passenger name: ", name, sizeof name))
            return;
        trim(name);
        if (valid_name(name))
            break;
        printf("  Enter a valid name (letters, spaces, . - ' only; max %d characters).\n",
               NAME_LEN - 1);
    }
    if (!read_int("  Age (1-120): ", 1, 120, &p.age))
        return;
    if (!read_gender(&p.gender))
        return;

    {
        char prompt[96];
        snprintf(prompt, sizeof prompt,
                 "\n  Seat %d, fare Rs. %d. Confirm booking? (y/n): ",
                 seat, trains[t].fare);
        if (!ask_yes_no(prompt)) {
            printf("  Booking cancelled. Nothing was saved.\n");
            return;
        }
    }

    p.pnr   = next_pnr++;
    p.train = t;
    p.seat  = seat;
    /* valid_name() above guarantees strlen(name) < NAME_LEN */
    memcpy(p.name, name, strlen(name) + 1);
    bookings[booking_count++] = p;

    print_ticket(&p, "CONFIRMED");
    if (!save_bookings())
        printf("  Warning: could not write %s - this booking will be lost on exit.\n",
               DATA_FILE);
}

static void check_availability(void)
{
    int t;

    printf("\n=== SEAT AVAILABILITY ===\n");
    t = select_train();
    if (t < 0)
        return;
    show_seat_matrix(t);
}

static void passenger_info(void)
{
    int choice;

    printf("\n=== PASSENGER INFORMATION ===\n");
    printf("  1. Search by PNR\n");
    printf("  2. List all passengers on a train\n");
    if (!read_int("\n  Choice (1-2): ", 1, 2, &choice))
        return;

    if (choice == 1) {
        long long pnr;
        int       i;

        if (!read_pnr("  Enter PNR: ", &pnr))
            return;
        i = find_by_pnr(pnr);
        if (i < 0)
            printf("  No booking found for PNR %lld.\n", pnr);
        else
            print_ticket(&bookings[i], "CONFIRMED");
    } else {
        int t, shown = 0;

        t = select_train();
        if (t < 0)
            return;
        printf("\n  Passengers on %d %s\n", trains[t].number, trains[t].name);
        printf("  Seat  PNR         Name                                                Age  Gender\n");
        printf("  ----  ----------  --------------------------------------------------  ---  ------\n");
        for (int s = 1; s <= SEATS_PER_TRAIN; s++) {
            int i = find_by_seat(t, s);
            if (i >= 0) {
                printf("  %-4d  %-10lld  %-50s  %-3d  %s\n",
                       s, bookings[i].pnr, bookings[i].name,
                       bookings[i].age, gender_text(bookings[i].gender));
                shown++;
            }
        }
        if (shown == 0)
            printf("  (no bookings yet)\n");
        else
            printf("\n  Total passengers: %d\n", shown);
    }
}

static void cancel_ticket(void)
{
    long long pnr;
    int       i, refund;

    printf("\n=== CANCEL A TICKET ===\n");
    if (!read_pnr("  Enter PNR: ", &pnr))
        return;
    i = find_by_pnr(pnr);
    if (i < 0) {
        printf("  No booking found for PNR %lld.\n", pnr);
        return;
    }

    print_ticket(&bookings[i], "CONFIRMED");
    refund = trains[bookings[i].train].fare * REFUND_PERCENT / 100;
    {
        char prompt[96];
        snprintf(prompt, sizeof prompt,
                 "\n  Cancel this ticket? Refund will be Rs. %d (%d%%) (y/n): ",
                 refund, REFUND_PERCENT);
        if (!ask_yes_no(prompt)) {
            printf("  Ticket kept. Nothing was changed.\n");
            return;
        }
    }

    memmove(&bookings[i], &bookings[i + 1],
            (size_t)(booking_count - i - 1) * sizeof(Passenger));
    booking_count--;

    printf("  Ticket %lld cancelled. Refund of Rs. %d will be processed.\n", pnr, refund);
    if (!save_bookings())
        printf("  Warning: could not write %s - this change will be lost on exit.\n",
               DATA_FILE);
}

/* ------------------------------------------------------------------ */
/* Main menu                                                           */
/* ------------------------------------------------------------------ */

int main(void)
{
    int choice;

#ifdef __EMSCRIPTEN__
    web_restore();
#endif
    load_bookings();
    printf("========================================\n");
    printf("     RAILWAY RESERVATION SYSTEM  (C)\n");
    printf("========================================\n");
    printf("  Loaded %d existing booking(s).\n", booking_count);

    for (;;) {
        printf("\n  1. Book a ticket\n");
        printf("  2. Check seat availability\n");
        printf("  3. Passenger information\n");
        printf("  4. Cancel a ticket\n");
        printf("  5. View trains\n");
        printf("  0. Exit\n");

        if (!read_int("\n  Enter choice: ", 0, 5, &choice))
            break;                              /* EOF (Ctrl+D / Ctrl+Z) */

        switch (choice) {
        case 1: book_ticket();        break;
        case 2: check_availability(); break;
        case 3: passenger_info();     break;
        case 4: cancel_ticket();      break;
        case 5: print_trains();       break;
        case 0:
            printf("\n  Thank you for using the Railway Reservation System.\n");
            return 0;
        }
    }

    printf("\n  Goodbye.\n");
    return 0;
}
