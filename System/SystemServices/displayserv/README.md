# displayserv

System display service built on `special/display`, `special/ipc` and the GUI
wire codecs. It handles output discovery and configuration, subscriptions,
presenter authorization, frame import and presentation completion events.

The executable requires `--presenter-token=<64 hex characters>`. Optional
arguments are `--backend=<name>`, `--device=<path>` and `--endpoint=<path>`;
the defaults are the software backend and `/run/punos/displayserv.sock`.

Build the service archive with `make`. Build its freestanding runtime test with
`make runtime-test`; `make check` builds and runs that test on a compatible
x86_64 Linux host.
