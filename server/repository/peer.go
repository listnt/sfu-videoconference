package repository

import (
	"sync"

	"github.com/listnt/videoconference/common"
	"go.uber.org/zap"
)

type PeerRepo interface {
	AddPeer(peer *common.Peer)
	RemovePeer(peer *common.Peer)
}

type peerRepo struct {
	logger *zap.Logger

	mu    *sync.Mutex
	peers map[string]*common.Peer
}

func NewPeerRepo(logger *zap.Logger) PeerRepo {
	return &peerRepo{
		logger: logger,
		mu:     &sync.Mutex{},
		peers:  make(map[string]*common.Peer),
	}
}

func (repo *peerRepo) AddPeer(peer *common.Peer) {
	repo.mu.Lock()
	defer repo.mu.Unlock()

	repo.peers[peer.Id] = peer
}

func (repo *peerRepo) RemovePeer(peer *common.Peer) {
	repo.mu.Lock()
	defer repo.mu.Unlock()

	delete(repo.peers, peer.Id)
}
