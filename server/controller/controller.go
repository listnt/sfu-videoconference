package controller

import (
	"encoding/json"
	"fmt"
	"time"

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

	ctrl := &controller{
		api:      api,
		logger:   logger,
		roomRepo: roomRepo,
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

	return func(t *webrtc.TrackRemote, _ *webrtc.RTPReceiver) {
		ctrl.logger.Info("track has been added", zap.String("kind", t.Kind().String()))

		// Create a track to fan out our incoming video to all peers
		trackLocal, err := webrtc.NewTrackLocalStaticRTP(t.Codec().RTPCodecCapability, t.ID(), t.StreamID())
		if err != nil {
			ctrl.logger.Error("failed to create TrackLocalStaticRTP", zap.Error(err))
			return
		}

		ctrl.roomRepo.AddTrack(peer.Id, trackLocal, msg.RoomId)
		defer ctrl.roomRepo.RemoveTrack(trackLocal, msg.RoomId)

		go ctrl.signalRoom(peer, msg.RoomId)

		buf := make([]byte, 1500)
		rtpPkt := &rtp.Packet{}

		for {
			i, _, err := t.Read(buf)
			if err != nil {
				return
			}

			if err = rtpPkt.Unmarshal(buf[:i]); err != nil {
				ctrl.logger.Error("failed to unmarshal RTP packet", zap.Error(err))

				return
			}

			rtpPkt.Extension = false
			rtpPkt.Extensions = nil

			if err = trackLocal.WriteRTP(rtpPkt); err != nil {
				return
			}
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

	for _, p := range peers {
		existingPeer := map[string]bool{}
		for _, sender := range p.PeerConnection.GetSenders() {
			if sender.Track() == nil {
				continue
			}

			existingPeer[sender.Track().ID()] = true
		}

		for _, reciever := range p.PeerConnection.GetReceivers() {
			if reciever.Track() == nil {
				continue
			}

			existingPeer[reciever.Track().ID()] = true
		}

		for _, track := range ctrl.roomRepo.GetTracks(room) {
			if _, ok := existingPeer[track.Track.ID()]; !ok {
				// Don't send track to self
				if track.PeerId == p.Id {
					continue
				}

				t, err := p.PeerConnection.AddTransceiverFromKind(track.Track.Kind(), webrtc.RTPTransceiverInit{
					Direction: webrtc.RTPTransceiverDirectionSendonly,
				})
				if err != nil {
					ctrl.logger.Error("failed to add track", zap.Error(err))
				}

				if t == nil {
					continue
				}

				if track.Track == nil {
					continue
				}

				t.Sender().ReplaceTrack(track.Track)
			}
		}

		offer, err := p.PeerConnection.CreateOffer(nil)
		if err != nil {
			ctrl.logger.Error("failed to create offer", zap.Error(err))
			continue
		}

		if err := p.PeerConnection.SetLocalDescription(offer); err != nil {
			ctrl.logger.Error("failed to set local description", zap.Error(err))
			continue
		}

		offerString, err := json.Marshal(offer)
		if err != nil {
			ctrl.logger.Error("failed to marshal offer", zap.Error(err))
			continue
		}

		if writeErr := p.SendMsg(&Msg{
			Type:   "offer",
			Data:   string(offerString),
			RoomId: room,
		}); writeErr != nil {
			ctrl.logger.Error("failed to write offer", zap.Error(writeErr))
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

			_ = peer.PeerConnection.WriteRTCP([]rtcp.Packet{
				&rtcp.PictureLossIndication{
					MediaSSRC: uint32(reciever.Track().SSRC()),
				},
			})
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
