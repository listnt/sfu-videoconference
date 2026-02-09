# videoconference

WebRTC SFU: WebSocket signaling, rooms, track fan-out. Server creates offers when peers join; new peer answers; answers are routed back to the correct offerer.

**Run**

```bash
go run .
```

Server listens on `:8085`. WebSocket endpoint: `/ws`.

**Client**

Open `index.html` (as file or via any static server). Enter a room ID, click Connect, choose a video file, then Start Video. Peers in the same room see each other’s streams.

**WebSocket messages** (JSON)

| Type      | Direction | Notes |
|-----------|-----------|--------|
| `join`    | client→server | `roomId` required |
| `leave`   | client→server | |
| `offer`   | server→client | Server sends; includes `offererId` |
| `answer`  | client→server | Client should send `offererId` from offer |
| `candidate` | both | ICE candidates |

**Layout**

- `main.go` — HTTP server, WebSocket upgrade, dispatch to controller
- `controller/` — connection handling, join/leave, offer/answer routing, track handling
- `repository/` — rooms, peers, tracks (in-memory)
- `common/` — Peer, SafeWebSocket
- `index.html` — browser client

**Stack**

Go, gorilla/websocket, pion/webrtc, zap.
