package main

import (
	"bytes"
	"log"
	"net/http"
	"strconv"
	"time"

	ws "github.com/gorilla/websocket"
)

const (
	address  = ":8080"
	codePath = "GET /sc/{code}"

	codeLength = 6

	// Every forwarded message is preceded by an envelope naming the sender.
	envelopePrefix = "peer "

	goneTag = "gone"
	failTag = "fail"

	// A description or candidate is a few kilobytes, so anything past this is
	// not a message this server should hold in memory.
	maxSignalBytes = 64 << 10

	readHeaderTimeout = 10 * time.Second
)

var upgrader = ws.Upgrader{
	CheckOrigin: func(*http.Request) bool {
		return true // just in dev
	},
}

func envelope(id int) []byte {
	return append([]byte(envelopePrefix), strconv.Itoa(id)...)
}

func parseEnvelope(msg []byte) (int, bool) {
	value, ok := bytes.CutPrefix(msg, []byte(envelopePrefix))
	if !ok {
		return 0, false
	}

	id, err := strconv.Atoi(string(value))
	if err != nil || id < 0 {
		return 0, false
	}

	return id, true
}

func validCode(code string) bool {
	if len(code) != codeLength {
		return false
	}

	for i := 0; i < len(code); i++ {
		upper := code[i] >= 'A' && code[i] <= 'Z'
		digit := code[i] >= '0' && code[i] <= '9'

		if !upper && !digit {
			return false
		}
	}

	return true
}

func (reg *registry) signal(w http.ResponseWriter, r *http.Request) {
	code := r.PathValue("code")
	if !validCode(code) {
		http.Error(w, "invalid connection code", http.StatusBadRequest)
		return
	}

	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}
	defer conn.Close()

	self := reg.join(code, conn)
	if self == nil {
		conn.WriteMessage(ws.TextMessage, []byte(failTag))
		log.Printf("full %s", code)
		return
	}

	log.Printf("join %s:%d", code, self.id)
	defer log.Printf("leave %s:%d", code, self.id)
	defer reg.leave(code, self.id)

	reg.relay(code, self)
}

// relay forwards one member's messages, which are addressed by the envelope the
// client sends, and rewritten to name the sender on the way out.
func (reg *registry) relay(code string, self *peer) {
	self.conn.SetReadLimit(maxSignalBytes)

	var to *peer

	for {
		kind, msg, err := self.conn.ReadMessage()
		if err != nil {
			if !ws.IsCloseError(err, ws.CloseNormalClosure,
				ws.CloseNoStatusReceived, ws.CloseGoingAway) {
				log.Printf("read %s:%d: %v", code, self.id, err)
			}
			return
		}

		if kind != ws.TextMessage {
			log.Printf("non-text message from %s:%d", code, self.id)
			return
		}

		if target, ok := parseEnvelope(msg); ok {
			to = reg.route(code, self, target)
			if to == nil {
				self.send([]byte(failTag))
				continue
			}

			to.send(envelope(self.id))
			continue
		}

		if to == nil {
			self.send([]byte(failTag))
			continue
		}

		to.send(msg)
	}
}

func main() {
	reg := newRegistry()

	mux := http.NewServeMux()
	mux.HandleFunc(codePath, reg.signal)

	server := &http.Server{
		Addr:              address,
		Handler:           mux,
		ReadHeaderTimeout: readHeaderTimeout,
	}

	log.Printf("listening on ws://localhost%s/sc/<code>", address)

	if err := server.ListenAndServe(); err != nil {
		log.Fatal(err)
	}
}
