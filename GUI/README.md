# GUI implementation

This tree is reserved for implementation owned by the GUI team.

```text
GUI/
├── Services/      displayserv, compositorserv, windowserv
├── Libraries/     displaylib, uilib, renderer, toolkit
├── Protocols/     shared public protocol headers and codecs
└── Applications/  desktop shell and system UI applications
```

The normative design and ownership rules live in `DevDoc/GUI/`. Generic
runtime, IPC, event-loop, memory, input-device, and process facilities remain
in the system layer and are consumed only through public interfaces.
