package common

import (
	"sync"

	"github.com/gorilla/websocket"
	"github.com/pion/webrtc/v4"
)

type Peer struct {
	Id             string
	PeerConnection *webrtc.PeerConnection
	RoomId         string
	websocket      *SafeWebSocket
	streams        map[string]*webrtc.TrackRemote
}

func NewPeer(peerConnection *webrtc.PeerConnection, c *SafeWebSocket) *Peer {
	return &Peer{
		Id:             peerConnection.ID(),
		PeerConnection: peerConnection,
		streams:        make(map[string]*webrtc.TrackRemote),
		websocket:      c,
	}
}

func (peer *Peer) SendMsg(msg any) error {
	peer.websocket.Lock()
	defer peer.websocket.Unlock()

	return peer.websocket.WriteJSON(msg)
}

type SafeWebSocket struct {
	*websocket.Conn
	sync.Mutex
}
