package controller

import (
	"encoding/json"
	"fmt"
	"net"
	"sync"
	"time"

	lru "github.com/hashicorp/golang-lru"
	"github.com/listnt/videoconference/common"
	"github.com/listnt/videoconference/repository"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
	"github.com/pion/webrtc/v4"
	"go.uber.org/zap"
)

type Msg struct {
	Type   string `json:"type"`
	Data   string `json:"data"`
	Sdp    string `json:"sdp"`
	RoomId string `json:"roomId"`
}

type Controller interface {
	HandleConnection(c *common.SafeWebSocket)
	JoinRoom(peer *common.Peer, msg Msg) error
	LeaveRoom(peer *common.Peer, msg Msg) error
}

type controller struct {
	logger *zap.Logger

	roomRepo repository.RoomRepo
	api      *webrtc.API

	simulcastLock         *lru.ARCCache
	trackExtentionHeaders map[string]map[string]common.RtpExtentions
	mu                    sync.Mutex

	conn net.Conn
}

func NewController(logger *zap.Logger, roomRepo repository.RoomRepo) Controller {
	settingEngine := webrtc.SettingEngine{}
	settingEngine.SetAnsweringDTLSRole(webrtc.DTLSRoleServer)
	mediaEngine := &webrtc.MediaEngine{}
	mediaEngine.RegisterDefaultCodecs()
	api := webrtc.NewAPI(
		webrtc.WithMediaEngine(mediaEngine),
		webrtc.WithSettingEngine(settingEngine),
	)

	cache, err := lru.NewARC(128)
	if err != nil {
		logger.Error("failed to create cache", zap.Error(err))
		panic(1)
	}

	conn, _ := net.Dial("udp", "127.0.0.1:44444")

	ctrl := &controller{
		api:           api,
		logger:        logger,
		roomRepo:      roomRepo,
		simulcastLock: cache,
		conn:          conn,
	}

	go func() {
		ticker := time.NewTicker(3 * time.Second)
		for _ = range ticker.C {
			roomIds := ctrl.roomRepo.GetRooms()
			for _, roomId := range roomIds {
				go ctrl.dispatch(roomId)
			}
		}
	}()

	return ctrl
}

func (ctrl *controller) HandleConnection(c *common.SafeWebSocket) {
	peerConnection, err := ctrl.api.NewPeerConnection(webrtc.Configuration{})

	if err != nil {
		ctrl.logger.Error("failed to create PeerConnection", zap.Error(err))
		return
	}

	defer peerConnection.Close()

	peer := common.NewPeer(peerConnection, c)

	peerConnection.OnConnectionStateChange(ctrl.onConnectionChange(peer))

	ctrl.logger.Debug("peer created", zap.String("id", peer.Id))

	for {
		_, msgBytes, err := c.ReadMessage()
		if err != nil {
			ctrl.logger.Info("failed to read message", zap.Error(err))

			break
		}

		msg := Msg{}
		err = json.Unmarshal(msgBytes, &msg)
		if err != nil {
			ctrl.logger.Error("failed to unmarshal msg", zap.Error(err))

			break
		}

		switch msg.Type {
		case "join":
			ctrl.JoinRoom(peer, msg)
		case "leave":
			ctrl.LeaveRoom(peer, msg)
		case "answer":
			ctrl.logger.Info("answer came", zap.String("id", peer.Id))

			answer := webrtc.SessionDescription{}
			if err := json.Unmarshal([]byte(msg.Data), &answer); err != nil {
				ctrl.logger.Error("failed to unmarshal answer", zap.Error(err))
				continue
			}

			if err := peer.PeerConnection.SetRemoteDescription(answer); err != nil {
				ctrl.logger.Error("failed to set remote description", zap.Error(err))
				continue
			}
		case "offer":
			offer := webrtc.SessionDescription{}
			if err := json.Unmarshal([]byte(msg.Data), &offer); err != nil {
				ctrl.logger.Error("failed to unmarshal offer", zap.Error(err))
				continue
			}

			if err := peer.PeerConnection.SetRemoteDescription(offer); err != nil {
				ctrl.logger.Error("failed to set remote description", zap.Error(err))
				continue
			}

			answer, err := peer.PeerConnection.CreateAnswer(nil)
			if err != nil {
				ctrl.logger.Error("failed to create answer", zap.Error(err))
				continue
			}

			if err := peer.PeerConnection.SetLocalDescription(answer); err != nil {
				ctrl.logger.Error("failed to set local description", zap.Error(err))
				continue
			}

			answerString, err := json.Marshal(answer)
			if err != nil {
				ctrl.logger.Error("failed to marshal answer", zap.Error(err))
				continue
			}

			if err := peer.SendMsg(&Msg{
				Type: "answer",
				Data: string(answerString),
			}); err != nil {
				ctrl.logger.Error("failed to write answer", zap.Error(err))
			}
		case "candidate":
			candidate := webrtc.ICECandidateInit{}
			if err := json.Unmarshal([]byte(msg.Data), &candidate); err != nil {
				ctrl.logger.Error("failed to unmarshal candidate", zap.Error(err))

				continue
			}

			ctrl.logger.Info("received candidate", zap.Any("candidate", candidate))

			if err := peerConnection.AddICECandidate(candidate); err != nil {
				ctrl.logger.Error("failed to add candidate", zap.Error(err))

				continue
			}
		}
	}
}

func (ctrl *controller) JoinRoom(peer *common.Peer, msg Msg) error {
	ctrl.logger.Info("join room", zap.String("roomId", msg.RoomId))

	for _, typ := range []webrtc.RTPCodecType{webrtc.RTPCodecTypeVideo, webrtc.RTPCodecTypeAudio} {
		if _, err := peer.PeerConnection.AddTransceiverFromKind(typ, webrtc.RTPTransceiverInit{
			Direction: webrtc.RTPTransceiverDirectionRecvonly,
		}); err != nil {
			ctrl.logger.Error("failed to create Transceiver", zap.Error(err))
			return err
		}
	}

	if msg.RoomId == "" {
		ctrl.logger.Error("room id is empty")
		return fmt.Errorf("room id is empty")
	}

	ctrl.roomRepo.JoinRoom(peer, msg.RoomId)

	peer.PeerConnection.OnICECandidate(ctrl.onICECandidate(peer, msg))

	// If PeerConnection is closed remove it from global list
	peer.PeerConnection.OnConnectionStateChange(ctrl.onConnectionStateChange(peer))

	peer.PeerConnection.OnTrack(ctrl.onTrack(peer, msg))

	peer.PeerConnection.OnICEConnectionStateChange(ctrl.onICEConnectionStateChange())

	// peer.PeerConnection.OnNegotiationNeeded(func() {
	// 	ctrl.logger.Info("negotiation needed")

	// 	go ctrl.signalRoom(peer, msg.RoomId)
	// })

	ctrl.signalRoom(peer, msg.RoomId)

	return nil
}

func (ctrl *controller) LeaveRoom(peer *common.Peer, msg Msg) error {
	// roomId := msg.Data
	// ctrl.roomRepo.LeaveRoom(peer, roomId)

	return nil
}

func (ctrl *controller) onICECandidate(peer *common.Peer, msg Msg) func(i *webrtc.ICECandidate) {
	// Trickle ICE. Emit server candidate to client
	return func(i *webrtc.ICECandidate) {
		if i == nil {
			return
		}
		// If you are serializing a candidate make sure to use ToJSON
		// Using Marshal will result in errors around `sdpMid`
		candidateString, err := json.Marshal(i.ToJSON())
		if err != nil {
			ctrl.logger.Error("failed to marshal candidate", zap.Error(err))

			return
		}

		ctrl.logger.Info("got candidate", zap.String("candidate", string(candidateString)))

		if writeErr := peer.SendMsg(&Msg{
			Type:   "candidate",
			Data:   string(candidateString),
			RoomId: msg.RoomId,
		}); writeErr != nil {
			ctrl.logger.Error("failed to write candidate", zap.Error(writeErr))
		}
	}
}

func (ctrl *controller) onConnectionStateChange(peer *common.Peer) func(p webrtc.PeerConnectionState) {
	return func(p webrtc.PeerConnectionState) {
		ctrl.logger.Info("peer connection state has changed", zap.String("state", p.String()))

		switch p {
		case webrtc.PeerConnectionStateFailed:
			if err := peer.PeerConnection.Close(); err != nil {
				ctrl.logger.Error("failed to close PeerConnection", zap.Error(err))
			}
		case webrtc.PeerConnectionStateClosed:
			// TODO : Remove from room and notify others
		default:
		}
	}
}

func (ctrl *controller) onTrack(peer *common.Peer, msg Msg) func(t *webrtc.TrackRemote, reciever *webrtc.RTPReceiver) {
	ctrl.logger.Debug("track handler has been added", zap.String("roomId", msg.RoomId))

	return func(t *webrtc.TrackRemote, reciever *webrtc.RTPReceiver) {
		ctrl.logger.Info("track has been added",
			zap.String("kind", t.Kind().String()),
			zap.String("id", t.ID()),
			zap.Uint32("ssrc", uint32(t.SSRC())),
			zap.String("rid", t.RID()),
			zap.Int("total tracks", len(reciever.Tracks())),
		)

		// Create a track to fan out our incoming video to all peers
		trackLocal, err := webrtc.NewTrackLocalStaticRTP(
			t.Codec().RTPCodecCapability,
			t.ID(),
			t.StreamID(),
			webrtc.WithRTPStreamID(t.RID()),
		)
		if err != nil {
			ctrl.logger.Error("failed to create TrackLocalStaticRTP", zap.Error(err))
			return
		}

		ctrl.roomRepo.AddTrack(peer.PeerConnection, trackLocal, msg.RoomId)
		defer func() {
			ctrl.roomRepo.RemoveTrack(trackLocal, msg.RoomId)
			ctrl.removeTrackFromPeers(trackLocal, msg.RoomId)
		}()

		counter, ok := ctrl.simulcastLock.Get(t.ID())
		if len(reciever.Tracks()) == 1 ||
			(ok &&
				(counter.(int)+1 == len(reciever.Tracks()))) { // simulcast
			go ctrl.signalRoom(peer, msg.RoomId)
		}
		ctrl.simulcastLock.Add(t.ID(), 1)

		buf := make([]byte, 1500)

		for {
			i, _, err := t.Read(buf)
			if err != nil {
				return
			}

			rtpPkt := &rtp.Packet{}
			if err = rtpPkt.Unmarshal(buf[:i]); err != nil {
				ctrl.logger.Error("failed to unmarshal RTP packet", zap.Error(err))

				return
			}

			if err = trackLocal.WriteRTP(rtpPkt); err != nil {
				return
			}

			// experiments, delete when stable
			b, _ := rtpPkt.Marshal()
			ctrl.conn.Write(b)
		}
	}
}

func (ctrl *controller) onICEConnectionStateChange() func(is webrtc.ICEConnectionState) {
	return func(is webrtc.ICEConnectionState) {
		ctrl.logger.Info("ice connection state has changed", zap.String("state", is.String()))
	}
}

func (ctrl *controller) signalRoom(peer *common.Peer, room string) {
	ctrl.roomRepo.LockRoom(room)
	defer ctrl.roomRepo.UnlockRoom(room)

	defer ctrl.dispatch(room)

	peers := ctrl.roomRepo.GetPeers(room)

	type updatePeerPair struct {
		p     *common.Peer
		offer webrtc.SessionDescription
	}

	updatedPeers := make([]updatePeerPair, 0)

	for _, p := range peers {
		if p.PeerConnection.ConnectionState() == webrtc.PeerConnectionStateClosed {
			continue
		}

		existingPeer := map[string][]string{}
		for _, sender := range p.PeerConnection.GetSenders() {
			if sender.Track() == nil {
				p.PeerConnection.RemoveTrack(sender)
				continue
			}

			existingPeer[sender.Track().ID()] = append(existingPeer[sender.Track().ID()], sender.Track().RID())
		}

		for _, reciever := range p.PeerConnection.GetReceivers() {
			if reciever.Track() == nil {
				continue
			}

			existingPeer[reciever.Track().ID()] = append(existingPeer[reciever.Track().ID()], reciever.Track().RID())
		}

		for _, track := range ctrl.roomRepo.GetTracks(room) {
			rids := existingPeer[track.Track.ID()]
			if common.Contains(rids, track.Track.RID()) {
				continue
			}

			if track.Track == nil {
				continue
			}

			// Don't send track to self
			if track.Peer.ID() == p.Id {
				continue
			}

			// if no sender is found, create a new one
			ctrl.logger.Info("adding track to new sender",
				zap.String("trackId", track.Track.ID()),
				zap.String("rid", track.Track.RID()),
			)

			t, err := p.PeerConnection.AddTransceiverFromTrack(track.Track, webrtc.RTPTransceiverInit{
				Direction: webrtc.RTPTransceiverDirectionSendonly,
			})
			if err != nil {
				ctrl.logger.Error("failed to add track", zap.Error(err))
			}
			if t == nil {
				continue
			}
		}

		if p.PeerConnection.ConnectionState() != webrtc.PeerConnectionStateClosed {
			offer, err := p.PeerConnection.CreateOffer(nil)
			if err != nil {
				ctrl.logger.Error("failed to create offer", zap.Error(err))
				continue
			}

			updatedPeers = append(updatedPeers, updatePeerPair{
				p:     p,
				offer: offer,
			})
		}
	}

	for _, p := range updatedPeers {
		if err := p.p.PeerConnection.SetLocalDescription(p.offer); err != nil {
			ctrl.logger.Error("failed to set local description", zap.Error(err))
			continue
		}

		offerString, err := json.Marshal(p.offer)
		if err != nil {
			ctrl.logger.Error("failed to marshal offer", zap.Error(err))
			continue
		}

		if err := p.p.SendMsg(&Msg{
			Type:   "offer",
			Data:   string(offerString),
			RoomId: room,
		}); err != nil {
			ctrl.logger.Error("failed to send offer", zap.Error(err))
		}
	}

}

func (ctrl *controller) dispatch(room string) {
	peers := ctrl.roomRepo.GetPeers(room)

	for _, peer := range peers {
		for _, reciever := range peer.PeerConnection.GetReceivers() {
			if reciever.Track() == nil {
				continue
			}

			for _, track := range reciever.Tracks() {
				_ = peer.PeerConnection.WriteRTCP([]rtcp.Packet{
					&rtcp.PictureLossIndication{
						MediaSSRC: uint32(track.SSRC()),
					},
				})
			}
		}
	}
}

func (ctrl *controller) onConnectionChange(peer *common.Peer) func(state webrtc.PeerConnectionState) {

	return func(state webrtc.PeerConnectionState) {
		// if state == webrtc.PeerConnectionStateClosed {
		// 	ctrl.roomRepo.LeaveRoom(peer, roomId)

		// 	for _, sender := range peer.PeerConnection.GetSenders() {
		// 		ctrl.roomRepo.RemoveTrack(sender.Track(), roomId)
		// 	}
		// }

		// return
	}
}

func (ctrl *controller) removeTrackFromPeers(track *webrtc.TrackLocalStaticRTP, roomId string) {
	peers := ctrl.roomRepo.GetPeers(roomId)
	ctrl.logger.Info("removing track", zap.String("trackID", track.ID()))

	for _, p := range peers {
		if p.PeerConnection.ConnectionState() == webrtc.PeerConnectionStateClosed {
			continue
		}

		for _, sender := range p.PeerConnection.GetSenders() {
			if sender.Track() == nil {
				p.PeerConnection.RemoveTrack(sender)
				continue
			}

			if sender.Track().ID() == track.ID() {
				p.PeerConnection.RemoveTrack(sender)
			}
		}

		offer, err := p.PeerConnection.CreateOffer(nil)
		if err != nil {
			ctrl.logger.Error("failed to create offer", zap.Error(err))
			continue
		}

		offerString, err := json.Marshal(offer)
		if err != nil {
			ctrl.logger.Error("failed to marshal offer", zap.Error(err))
			continue
		}

		if err := p.SendMsg(&Msg{
			Type:   "offer",
			Data:   string(offerString),
			RoomId: roomId,
		}); err != nil {
			ctrl.logger.Error("failed to send offer", zap.Error(err))
		}
	}

}
