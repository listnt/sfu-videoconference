package repository

import (
	"sync"

	"github.com/listnt/videoconference/common"
	"github.com/pion/webrtc/v4"
	"go.uber.org/zap"
	"golang.org/x/exp/maps"
)

type RoomRepo interface {
	JoinRoom(peer *common.Peer, room string)
	AddTrack(peerId string, track *webrtc.TrackLocalStaticRTP, room string)
	LeaveRoom(peer *common.Peer, room string)
	RemoveTrack(track *webrtc.TrackLocalStaticRTP, room string)
	GetPeers(room string) map[string]*common.Peer
	GetTracks(room string) map[string]Track
	LockRoom(room string)
	UnlockRoom(room string)
	GetRooms() []string
}

type roomRepo struct {
	logger *zap.Logger

	mu    *sync.Mutex
	rooms map[string]*Room
}

type Room struct {
	mu     *sync.Mutex
	peers  map[string]*common.Peer
	tracks map[string]Track
}

type Track struct {
	Track  *webrtc.TrackLocalStaticRTP
	PeerId string
}

func NewRoomRepo(logger *zap.Logger) RoomRepo {
	return &roomRepo{
		logger: logger,
		mu:     &sync.Mutex{},
		rooms:  make(map[string]*Room),
	}
}

func (repo *roomRepo) JoinRoom(peer *common.Peer, room string) {
	repo.mu.Lock()
	defer repo.mu.Unlock()

	if _, ok := repo.rooms[room]; !ok {
		repo.rooms[room] = &Room{
			mu:     &sync.Mutex{},
			peers:  make(map[string]*common.Peer),
			tracks: make(map[string]Track),
		}
	}

	repo.rooms[room].peers[peer.Id] = peer
}

func (repo *roomRepo) LeaveRoom(peer *common.Peer, room string) {
	repo.mu.Lock()
	defer repo.mu.Unlock()

	delete(repo.rooms[room].peers, peer.Id)

	if len(repo.rooms[room].peers) == 0 {
		delete(repo.rooms, room)
	}
}

func (repo *roomRepo) AddTrack(peerId string, track *webrtc.TrackLocalStaticRTP, room string) {
	repo.rooms[room].mu.Lock()
	defer repo.rooms[room].mu.Unlock()

	repo.rooms[room].tracks[track.ID()] = Track{
		Track:  track,
		PeerId: peerId,
	}
}

func (repo *roomRepo) RemoveTrack(track *webrtc.TrackLocalStaticRTP, room string) {
	repo.rooms[room].mu.Lock()
	defer repo.rooms[room].mu.Unlock()

	delete(repo.rooms[room].tracks, track.ID())
}

func (repo *roomRepo) GetPeers(room string) map[string]*common.Peer {
	return repo.rooms[room].peers
}

func (repo *roomRepo) GetTracks(room string) map[string]Track {
	return repo.rooms[room].tracks
}

func (repo *roomRepo) LockRoom(room string) {
	repo.rooms[room].mu.Lock()
}

func (repo *roomRepo) UnlockRoom(room string) {
	repo.rooms[room].mu.Unlock()
}

func (repo *roomRepo) GetRooms() []string {
	return maps.Keys(repo.rooms)
}
