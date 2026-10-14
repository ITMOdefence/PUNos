# initserv v1

Single-threaded native PUNos service supervisor. Specification:
`DevDoc/SystemServices/base/initserv.md`. It exposes no IPC endpoint and
accepts no runtime arguments or registry files.

The build-time generator compiles ServiceSpecs from PlatformRegistry, checks
service IDs/paths/vectors/policies and generates the same PlatformRegistryId
and native application catalogue as appserv. Native apps are not boot services.
The default platform registry enables logserv and appserv only. Known service
order is logserv, appserv, IOserv, locationserv, camserv; other registered
services follow in source order. Disabled entries are never started.

Startup creates one event loop, launches logserv, verifies a connection to
its endpoint, submits the startup record, and then launches enabled services.
The default logserv startup timeout is 5000 ms (the specification leaves its
numeric value unspecified). No readiness is inferred for other services.
Before logger readiness, failures are reported only by initialization exit
code 3. Non-logger launch failures do not tear down existing services; they are
journaled and retried only when the registry requests restart, honoring the
specification's contract 8. Logserv startup/recovery failure is fatal.

A 1000 ms timer monitors owned child ProcessHandles through process_poll.
Every restart has a one-shot timer: delays 1000, 2000, 4000, 8000, 16000, 30000
ms, capped at 30000; successful process_start resets the delay to 1000.
Completed handles are reaped before any restart. Due deadlines are checked
on the owner even when a temporary logging connection consumed another timer
event, preventing missed restarts. libprocess's successful fork can still
be followed by exec failure exit 127; this is monitored as a normal child exit.

SIGTERM/SIGINT provide the unspecified v1 source of an internal Stopping
request; they do not reboot or unmount the OS. Stopping cancels restarts,
journals the transition while logserv is available, sends TERM in reverse
start order, then waits/reaps every remaining handle. Interrupted waits are
retried. There is no SIGKILL or child shutdown timeout. All registered services
must honor SIGTERM. Temporary logging connections close after acknowledgement
or error. Logger downtime retains the last child exit diagnostics in memory
and submits them after logger recovery; no durable guarantee is possible while
logserv is absent.

`make check` verifies registry generation and the delay cap. Parent service
checks exercise genuine logserv/appserv processes, startup failure gating,
disabled entries, normal exit restarts, logger recovery, child ownership,
noncritical launch faults/backoff and shutdown. Test process wrappers are not
part of the production executable.
