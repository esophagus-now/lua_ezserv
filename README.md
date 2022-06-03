# ezserv

_Project started on May 28 2022_

The goal here is to make the simplest possible
asynchronous websocket server. Because my favourite
prorgamming language is Lua, I decided to make a 
luarock that I can hopefully share with others.

The goal of this project is to make it easy to add
webserver support to small projects. For example, so
that you can easily add web GUIs to your code, or for
making small web utilities. 

Design goals:
- It must be possible for a beginner (who is familiar with
  Lua) to write a file-serving HTTP server in **under** 20
  minutes without copy-pasting
- The Lua API should not be forced to use callbacks.
- Performance and scalability are a secondary concern. Don't
  get the wrong idea; I'm perfectly capable of writing a
  high-performance library, but the goal here is API
  friendliness.
- I am willing to give up on low-level flexibility in
  most cases :shrug:. For example,
  - Constrained to IPv4
  - Always uses `INADDR_ANY`
  - `SO_REUSEADDRESS` is always on
  - Servers are always listening
  - Everything uses the same `io_context` in a single thread
  - And more...
- Let me reiterate that the goal is API friendliness.
- Let me reiterate once again that the goal is API friendliness.

## Overview

This library defines three new userdata types:
- `ezserver`, a handle to a Boost TCP acceptor and
  the required Boost ASIO io_context.
- `ezhttp`, a handle to a Boost TCP socket (plus
  all the necessary buffers that Boost wants)
- `ezwebsock`, a handle to a Boost beast websocket
  stream (plus all the necessary buffers)

When a C++ function returns one of these userdata types, 
it will check if the userdata already exists in Lua. If
so, it returns a copy of the userdata. This is because 
we want the userdata to be usable as table keys.

### The `ezserv` table
When the ezserv library is loaded in Lua, it will create
a global table called `ezserv` that contains:
- `start_server([port])`, a function that create a
  new `ezserver` on `port` (default=80)
- `http`, a list of HTTP error codes. See below for a list

Note: the call to `require "ezserv"` returns this table.

### The `ezserver` type
The member functions of an `ezserver` are:
- `accept()`: asynchronously accept a new HTTP connection
- `next_event()`: block until any event on this server. 
  This includes new connections, incoming request/data,
  and errors. See lower down for a list of event types.

### The `ezhttp` type
The member functions of an `ezhttp` are:
- `recv()`: Begin an asynchronous read. At some point in
  the future, calling `server:next_event()` will return
  the result of this read.
- `send(str|error_code)`: Passing a Lua string to this
  function will send an ordinary HTTP response with `str`
  as its body. Passing a member of `ezserv.http` will send
  an HTTP error code.

### The `ezwebsock` type
TODO

## Quickstart

```lua
-- Load the ezserv module
ez = require "ezserv"

-- Start a server on port 8080. Behind the scenes, this
-- - Constructs a boost::asio::tcp::acceptor for IPv4
-- - Sets its reuse_address option to true
-- - Binds the acceptor to INADDR_ANY the given port number
s = ez.start_server(8080)

-- Accept an HTTP connection
s:accept()

-- ezserver:accept (and many other functions) are 
-- asynchronous. The process has started, but we 
-- need to wait until it is done
event, obj = s:next_event()

-- event is a table. All events have a "type" field
-- to say what they are.
print("Received an event: ", event.type)
-- The obj is a userdata for the relevant object. In
-- this case, an ezhttp
print(obj)

-- Start waiting for a request
obj:recv()
event, obj2 = s:next_event()
print("Received another event: ", event.type)
assert(obj2 == obj)
obj2:send("<html>Hello world</html>")
```

