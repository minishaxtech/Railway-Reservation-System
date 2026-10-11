# Railway Reservation System (C)

[![Build and test](../../actions/workflows/ci.yml/badge.svg)](../../actions/workflows/ci.yml)

A console-based railway reservation system written in **pure C** (C99, standard library only, no external dependencies). Bookings are saved to a text file, so they are still there the next time you run it.

## Features

| Feature | What it does |
|---|---|
| **Ticket booking** | Pick a train, pick a seat (or let the system auto-assign one), enter passenger details, confirm, and get a ticket with a unique PNR |
| **Seat availability** | Shows the 40-seat matrix for a train: `[XX]` = booked, `[nn]` = free, plus a free-seat count |
| **Passenger information** | Look up a ticket by PNR, or list every passenger on a train |
| **Ticket cancellation** | Cancel by PNR with a confirmation step; 80% of the fare is refunded |

## Build and run

You need a C compiler (`gcc` or `clang`) and `make`.

```bash
make          # build
make run      # build and start
make test     # run the automated smoke tests
make clean    # remove the binary and bookings.txt
```

No `make`? Compile directly:

```bash
gcc -std=c99 -Wall -Wextra -o railway src/main.c
./railway
```

**Windows:** install [MinGW-w64](https://www.mingw-w64.org/) or use WSL, then run the same commands. (`railway.exe` is produced on MinGW.)

## Run it in the browser (GitHub Pages)

The same C code is compiled to WebAssembly with [Emscripten](https://emscripten.org/) and shown on a web page (`web/index.html`). GitHub Actions rebuilds and publishes it on every push to `main` (`.github/workflows/pages.yml`).

Live site: `https://<your-username>.github.io/<repo-name>/`

Only the very bottom layer differs in the browser build (reading a line of input, and saving `bookings.txt` to the browser's localStorage). Those parts are wrapped in `#ifdef __EMSCRIPTEN__` in `src/main.c`; all booking logic is shared with the desktop build.

To build the web version yourself you need the Emscripten SDK, then `make web` and serve the `web/` folder (for example `python -m http.server -d web`).

## Sample session

```
========================================
     RAILWAY RESERVATION SYSTEM  (C)
========================================
  Loaded 0 existing booking(s).

  1. Book a ticket
  2. Check seat availability
  3. Passenger information
  4. Cancel a ticket
  5. View trains
  0. Exit

  Enter choice: 1
  ...
  +------------------------------------------+
  |  E-TICKET - CONFIRMED                    |
  +------------------------------------------+
   PNR          : 1000000001
   Train        : 12951 Mumbai Rajdhani
   Route        : Mumbai Central -> New Delhi
   Departure    : 16:35
   Passenger    : Asha Verma
   Age / Gender : 25 / Female
   Seat No.     : 1
   Fare         : Rs. 2450
  +------------------------------------------+
```

Seat matrix:

```
  [XX] [ 2] [ 3] [ 4] [XX] [ 6] [ 7] [ 8]
  [ 9] [10] [11] [12] [13] [14] [15] [16]
  ...
  Available: 38 / 40
```

## How it works

**Data.** Two `struct`s hold everything:

- `Train` - number, name, route, departure time, fare. The 5 trains are a fixed sample table at the top of `src/main.c`; edit it to add your own.
- `Passenger` - PNR, which train, seat number, age, gender, name.

All bookings live in one array, `bookings[]`, with a counter `booking_count`. A seat is "booked" if some passenger in the array has that train and seat number, so there is no separate seat grid to keep in sync.

**PNR.** Each ticket gets the next number from a counter (starting at 1000000001). The counter is saved in the data file, so a PNR is **never reused**, even after a cancellation.

**Cancellation.** Finds the passenger by PNR, asks for confirmation, then removes the entry by shifting later entries down one slot (`memmove`).

**Persistence.** After every booking or cancellation the whole array is written to `bookings.txt`:

```
NEXT_PNR 1000000003
1000000001 0 5 25 F Asha Verma
```

On startup the file is read back, and any line that is damaged or contradicts another (duplicate seat, duplicate PNR, bad age) is skipped with a warning instead of crashing.

**Input safety.** Every read is a whole line parsed with `fgets` + `strtol`, so typing letters where a number is expected, pasting a very long line, or hitting Ctrl+D never crashes or loops forever.

## Project structure

```
railway-reservation-system/
├── src/main.c               # the entire program
├── web/index.html           # browser terminal for the WebAssembly build
├── tests/smoke_test.sh      # scripted end-to-end tests
├── Makefile
├── .github/workflows/ci.yml    # build + test on every push
├── .github/workflows/pages.yml # build WebAssembly + publish the website
├── .gitignore
└── README.md
```

## Ideas to extend it

- Add waiting-list handling when a train is full
- Different coach classes (Sleeper / 3AC / 2AC) with different fares
- Search trains by source and destination
- Travel dates, so each date has its own seat matrix

## Team

Team 9

| Name | Roll no. |
|---|---|
| Kanishk Sriwastava | 2602030078 |
| Minisha Bhardwaj | 2602030055 |
