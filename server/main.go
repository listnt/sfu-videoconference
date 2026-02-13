package main

import (
	"fmt"
	"net/http"
	"sync"

	"github.com/gorilla/websocket"
	"github.com/listnt/videoconference/common"
	"github.com/listnt/videoconference/controller"
	"github.com/listnt/videoconference/repository"
	"go.uber.org/zap"
	"go.uber.org/zap/zapcore"
)

var logger *zap.Logger

type Server struct {
	controller controller.Controller
}

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

func main() {
	fmt.Println("Starting server...")
	c := zap.NewProductionConfig()
	c.Level = zap.NewAtomicLevelAt(zapcore.DebugLevel)
	logger, _ := c.Build()

	roomRepo := repository.NewRoomRepo(logger)
	ctrl := controller.NewController(logger, roomRepo)

	srv := Server{
		controller: ctrl,
	}

	http.HandleFunc("/ws", srv.WebSocket)

	http.ListenAndServe(":8085", nil)
}

func (srv *Server) WebSocket(w http.ResponseWriter, r *http.Request) {
	ws, _ := upgrader.Upgrade(w, r, nil)

	c := &common.SafeWebSocket{ws, sync.Mutex{}}
	defer c.Close()

	srv.controller.HandleConnection(c)
}
