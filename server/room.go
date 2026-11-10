package main

import (
	"sync"

	ws "github.com/gorilla/websocket"
)

const (
	hostID    = 0
	maxGuests = 9
)

// peer serialises writes to one socket, because the read loops of the other
// members of the room forward into it.
type peer struct {
	id   int
	conn *ws.Conn
	mu   sync.Mutex
}

func (p *peer) send(frames ...[]byte) bool {
	p.mu.Lock()
	defer p.mu.Unlock()

	for _, frame := range frames {
		if err := p.conn.WriteMessage(ws.TextMessage, frame); err != nil {
			return false
		}
	}

	return true
}

// room keeps its host for the life of the session, so a guest can arrive or
// leave at any time without invalidating the connection code.
type room struct {
	mu     sync.Mutex
	host   *peer
	guests map[int]*peer
	next   int
}

func newRoom() *room {
	return &room{guests: make(map[int]*peer)}
}

func (r *room) join(conn *ws.Conn) *peer {
	r.mu.Lock()
	defer r.mu.Unlock()

	if r.host == nil {
		r.host = &peer{id: hostID, conn: conn}
		return r.host
	}

	if len(r.guests) >= maxGuests {
		return nil
	}

	r.next++
	joined := &peer{id: r.next, conn: conn}
	r.guests[joined.id] = joined
	return joined
}

// leave reports a departure and answers whether the room is now empty. A guest's
// departure goes to the host, and a host's departure closes every guest.
func (r *room) leave(id int) bool {
	r.mu.Lock()

	if id == hostID {
		guests := r.guests
		r.host = nil
		r.guests = make(map[int]*peer)
		r.mu.Unlock()

		for _, guest := range guests {
			guest.send(envelope(hostID), []byte(goneTag))
			guest.conn.Close()
		}

		return true
	}

	guest, ok := r.guests[id]
	if !ok {
		r.mu.Unlock()
		return false
	}

	delete(r.guests, id)
	host := r.host
	empty := host == nil && len(r.guests) == 0
	r.mu.Unlock()

	if host != nil {
		host.send(envelope(guest.id), []byte(goneTag))
	}

	return empty
}

// route resolves the peer a message is addressed to. A guest may only address
// the host; the host addresses a guest by id.
func (r *room) route(from *peer, target int) *peer {
	r.mu.Lock()
	defer r.mu.Unlock()

	if from.id == hostID {
		return r.guests[target]
	}

	return r.host
}

type registry struct {
	mu    sync.Mutex
	rooms map[string]*room
}

func newRegistry() *registry {
	return &registry{rooms: make(map[string]*room)}
}

func (reg *registry) join(code string, conn *ws.Conn) *peer {
	reg.mu.Lock()
	found, ok := reg.rooms[code]
	if !ok {
		found = newRoom()
		reg.rooms[code] = found
	}
	reg.mu.Unlock()

	return found.join(conn)
}

func (reg *registry) leave(code string, id int) {
	reg.mu.Lock()
	found, ok := reg.rooms[code]
	reg.mu.Unlock()

	if !ok {
		return
	}

	if !found.leave(id) {
		return
	}

	reg.mu.Lock()
	defer reg.mu.Unlock()

	if reg.rooms[code] == found {
		delete(reg.rooms, code)
	}
}

func (reg *registry) route(code string, from *peer, target int) *peer {
	reg.mu.Lock()
	found, ok := reg.rooms[code]
	reg.mu.Unlock()

	if !ok {
		return nil
	}

	return found.route(from, target)
}
