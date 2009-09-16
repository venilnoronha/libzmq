/*
    Copyright (c) 2007-2009 FastMQ Inc.

    This file is part of 0MQ.

    0MQ is free software; you can redistribute it and/or modify it under
    the terms of the Lesser GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    0MQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "zmq_connecter.hpp"
#include "zmq_connecter_init.hpp"
#include "io_thread.hpp"
#include "err.hpp"

zmq::zmq_connecter_t::zmq_connecter_t (io_thread_t *parent_,
      socket_base_t *owner_, const options_t &options_,
      const char *session_name_, bool wait_) :
    owned_t (parent_, owner_),
    io_object_t (parent_),
    handle_valid (false),
    wait (wait_),
    options (options_),
    session_name (session_name_)
{
}

zmq::zmq_connecter_t::~zmq_connecter_t ()
{
}

int zmq::zmq_connecter_t::set_address (const char *addr_)
{
     return tcp_connecter.set_address (addr_);
}

void zmq::zmq_connecter_t::process_plug ()
{
    if (wait)
        add_timer ();
    else
        start_connecting ();
    owned_t::process_plug ();
}

void zmq::zmq_connecter_t::process_unplug ()
{
    if (wait)
        cancel_timer ();
    if (handle_valid)
        rm_fd (handle);
}

void zmq::zmq_connecter_t::in_event ()
{
    //  We are not polling for incomming data, so we are actually called
    //  because of error here. However, we can get error on out event as well
    //  on some platforms, so we'll simply handle both events in the same way.
    out_event ();
}

void zmq::zmq_connecter_t::out_event ()
{
    fd_t fd = tcp_connecter.connect ();
    rm_fd (handle);
    handle_valid = false;

    //  Handle the error condition by attempt to reconnect.
    if (fd == retired_fd) {
        tcp_connecter.close ();
        wait = true;
        add_timer ();
        return;
    }

    //  Create an init object. 
    io_thread_t *io_thread = choose_io_thread (options.affinity);
    zmq_connecter_init_t *init = new zmq_connecter_init_t (io_thread, owner,
        fd, options, session_name.c_str ());
    zmq_assert (init);
    send_plug (init);
    send_own (owner, init);

    //  Ask owner socket to shut the connecter down.
    term ();
}

void zmq::zmq_connecter_t::timer_event ()
{
    wait = false;
    start_connecting ();
}

void zmq::zmq_connecter_t::start_connecting ()
{
    //  Open the connecting socket.
    int rc = tcp_connecter.open ();

    //  Connect may succeed in synchronous manner.
    if (rc == 0) {
        out_event ();
        return;
    }

    //  Connection establishment may be dealyed. Poll for its completion.
    else if (rc == -1 && errno == EAGAIN) {
        handle = add_fd (tcp_connecter.get_fd ());
        handle_valid = true;
        set_pollout (handle);
        return;
    }

    //  TODO: Handle the error condition by eventual reconnect.
    zmq_assert (false);
}
