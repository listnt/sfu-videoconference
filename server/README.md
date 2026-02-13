# videoconference

WebRTC SFU: WebSocket signaling, rooms, track fan-out. When peers join a room, the server creates offers (e.g. to add new tracks) and sends them to clients; each client answers on its own connection and the server applies the answer to that peer’s connection.

**Run**

```bash
go run .
```

Server listens on `:8085`. WebSocket endpoint: `/ws`.

**Client**

Open `index.html` (as file or via any static server). Enter a room ID, click Connect, choose a video file, then Start Video. Peers in the same room see each other’s streams.

**WebSocket messages** (JSON)

| Type        | Direction     | Notes |
|-------------|---------------|--------|
| `join`      | client→server | `roomId` required |
| `leave`     | client→server | (handled; leave logic not fully implemented) |
| `offer`     | server→client | Server sends; payload in `data`, `roomId` set |
| `answer`    | client→server | Payload in `data`; server associates by connection |
| `candidate` | both          | ICE candidates; payload in `data` |

**Layout**

- `main.go` — HTTP server, WebSocket upgrade, dispatch to controller
- `controller/` — connection handling, join/leave, offer/answer routing, track handling
- `repository/` — rooms, peers, tracks (in-memory)
- `common/` — Peer, SafeWebSocket
- `index.html` — browser client

**Stack**

Go, gorilla/websocket, pion/webrtc, zap.
