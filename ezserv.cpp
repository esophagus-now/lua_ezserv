#include <iostream>
#include <string>
#include <sstream>
#include <lua.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/system/error_code.hpp>
#include <functional>
#include <memory>
#include <utility>
#include <typeinfo>
#include <cstring> //memcpy
#include <unordered_map>

using namespace std;
using namespace std::placeholders;
using namespace boost::asio::ip;
using flat_buffer = boost::beast::flat_buffer;
namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;
using errcode = boost::system::error_code;

extern "C"
int luaopen_ezserv(lua_State*);

template <typename CRTP>
struct refcounted {
    int *refcount;

    refcounted() : refcount(new int) {
        //cout << "Constructing a " << typeid(CRTP).name() << endl;
        *refcount = 1;
    }

    CRTP* ref() {
        //cout << "ref()ing a " << typeid(CRTP).name() << endl;
        *refcount += 1;
        return static_cast<CRTP*>(this);
    }

    void put() {
        //cout << "put()ing a " << typeid(CRTP).name() << endl;
        *refcount -= 1;
        if (*refcount == 0) {
            //cout << "destroying a " << typeid(CRTP).name() << endl;
            delete refcount;
            delete static_cast<CRTP*>(this);
        }
    }
};

struct ezserver : refcounted<ezserver> {
    boost::asio::io_context ctx;
    tcp::acceptor acc;
    int port;

    //Have I ever mentioned that I don't like constructors?
    ezserver() : ctx(), acc(ctx, tcp::v4()) {}

    ~ezserver() {
        //cout << "Destroying an ezserver" << endl;
    }
};

struct ezhttp : refcounted<ezhttp> {
    tcp::socket sock;
    flat_buffer req_buf;
    http::request<http::string_body> req;
    
    http::response<http::buffer_body> rsp;
    bool rsp_vld;

    ezhttp(tcp::socket sock) : 
      sock(move(sock)),
      rsp_vld(false)
    {}

    ~ezhttp() {
        //cout << "Destroying an ezhttp" << endl;
    }
};

struct ezwebsock : refcounted<ezwebsock> {
    websocket::stream<tcp::socket> sock;
    flat_buffer buf;

    ezwebsock(tcp::socket sock) :
        sock(move(sock))
    {}
};

void luaL_pusherrorcode(lua_State *L, errcode const& ec) {
    lua_newtable(L);
    lua_pushstring(L, "type");
    lua_pushstring(L, "error");
    lua_rawset(L,-3);

    //Would be nice if boost error code just used an enum
    //so that we could export some constants and let the 
    //user's Lua code check them... but no... everything 
    //in boost has to be complicated... the best idea I
    //have is to just push the message.
    lua_pushstring(L, "message");
    lua_pushstring(L, ec.message().c_str());
    lua_rawset(L,-3);
}

//This function will take care of calling ref() on ptr if
//it is needed
template <typename T>
void luaL_pushptr(lua_State *L, T *ptr, char const *tp) {
    //First check if this userdata already exists. This
    //is so that the Lua code can use the userdatas we
    //we give it as table keys.
    lua_pushlightuserdata(L, reinterpret_cast<void*>(&luaopen_ezserv));
    lua_gettable(L, LUA_REGISTRYINDEX); //Get the udata lookup table
    lua_pushlightuserdata(L, ptr);
    lua_rawget(L,-2);

    if (lua_type(L,-1) == LUA_TUSERDATA) {
        //This userdata already exists. This is an empty
        //if body, but I left it so I could write this
        //comment :-)

        //At this point, the stack contains the udata
        //lookup table, and the userdata
    } else {
        assert(lua_type(L,-1) == LUA_TNIL);
        lua_pop(L,1);

        //Top of stack still contains udata lookup table
        
        //Create a new userdata and save it in our
        //registry of existent userdatas.
        T** u = reinterpret_cast<T**> (
            lua_newuserdata(L, sizeof(T*))
        );
        *u = ptr->ref();
        luaL_setmetatable(L, tp);

        lua_pushlightuserdata(L, ptr);
        lua_pushvalue(L, -2); //Copy the new userdata...
        lua_rawset(L,-4); //...and save it in the udata lookup table

        //Now the stack contains the udata lookup table and
        //the new userdata
    }
    
    lua_remove(L,-2); //Pop the udata lookup table
    return;
}

template <typename T>
inline T* luaL_checkptr(lua_State *L, int index, char const *tp) {
    return *reinterpret_cast<T**>(
        luaL_checkudata(L, index, tp)
    );
}

//Rule: anything that the callback pushes to the stack
//is returned by ezserver:next_event. A callback can push
//either the event table and the source object, or nothing
//at all. If nothing is pushed, ezserver:next_event should
//wait for the next callback.
//Subtle: we only pass in the ezerver pointer as a way to
//"extend its lifetime", or to be more precise, so we can
//call s->put() at the right time. Remember that we aren't 
//using shared_ptr anyomre :-( 
void accept_handler(
    lua_State *L, ezserver *s, 
    errcode const& ec, tcp::socket sock
) {
    //cout << "In accept handler" << endl;
    
    if (ec) {
        luaL_pusherrorcode(L, ec);
        lua_pushnil(L);
        s->put();
        return;
    }

    lua_newtable(L);
    lua_pushstring(L, "type");
    lua_pushstring(L, "connect");
    lua_rawset(L, -3);

    ezhttp *ret = new ezhttp(move(sock));

    luaL_pushptr(L, ret, "ezhttp");
    //Subtle: pushptr will call ref() if needed. We
    //still need to call put() to release the references
    //held by this handler call
    ret->put();
    //And here we simply release the pointer that we
    //ref'd to keep it alive until the handler was
    //called
    s->put();
    
    return;
}

void ws_accept_handler(
    lua_State *L,
    ezhttp *h, ezwebsock *s,
    errcode const& ec
) {
    if (ec) {
        luaL_pusherrorcode(L, ec);
        lua_pushstring(L, "http_obj");
        luaL_pushptr(L, h, "ezhttp");
        lua_rawset(L,-3);
        
        luaL_pushptr(L, s, "ezwebsock");
        //Subtle: pushptr will call ref() if needed. We
        //still need to call put() to release the references
        //held by this handler call
        h->put();
        s->put();
        
        return;
    }
    
    lua_newtable(L);
    lua_pushstring(L, "type");
    lua_pushstring(L, "connect");
    lua_rawset(L, -3);
    
    lua_pushstring(L, "http_obj");
    luaL_pushptr(L, h, "ezhttp");
    lua_rawset(L,-3);
    
    luaL_pushptr(L, s, "ezwebsock");
    //Subtle: pushptr will call ref() if needed. We
    //still need to call put() to release the references
    //held by this handler call
    h->put();
    s->put();

    return;
}

void http_send_handler(
    lua_State *L, ezhttp *s,
    char *data, bool en_ack,
    errcode const& ec, size_t bytes
) {
    //cout << "In HTTP send handler" << endl;
    if (ec) {
        luaL_pusherrorcode(L, ec);
        luaL_pushptr(L, s, "ezhttp");
        s->put();
        return;
    }
    
    //TODO: do I need to check if this actually sent
    //all the bytes? Who knows

    if (data != nullptr) delete[] data; //No longer needed
    s->rsp_vld = false; //No more responses in flight

    if (en_ack) {
        lua_newtable(L);
        lua_pushstring(L, "type");
        lua_pushstring(L, "ack");
        lua_rawset(L,-3);

        lua_pushstring(L, "bytes");
        lua_pushnumber(L, bytes);
        lua_rawset(L,-3);

        luaL_pushptr(L, s, "ezhttp");
    }

    //Subtle: pushptr will call ref() if needed. We would
    //still need to call put() to release the references
    //held by this handler call
    s->put();
}

void websock_send_handler(
    lua_State *L, ezwebsock *s,
    char *data, bool en_ack,
    errcode const& ec, size_t bytes
) {
    //cout << "In websock send handler" << endl;
    if (ec) {
        luaL_pusherrorcode(L, ec);
        luaL_pushptr(L, s, "ezwebsock");
        s->put();
        return;
    }
    
    //TODO: do I need to check if this actually sent
    //all the bytes? Who knows

    if (data != nullptr) delete[] data; //No longer needed

    if (en_ack) {
        lua_newtable(L);
        lua_pushstring(L, "type");
        lua_pushstring(L, "ack");
        lua_rawset(L,-3);

        lua_pushstring(L, "bytes");
        lua_pushnumber(L, bytes);
        lua_rawset(L,-3);

        luaL_pushptr(L, s, "ezwebsock");
    }

    //Subtle: pushptr will call ref() if needed. We would
    //still need to call put() to release the references
    //held by this handler call
    s->put();
}

void http_recv_handler(
    lua_State *L, ezhttp *s,
    errcode const& ec, size_t bytes
) {
    //cout << "In http recv handler" << endl;
    if (ec) {
        luaL_pusherrorcode(L, ec);
        lua_pushnil(L);
        s->put();
        return;
    }

    lua_newtable(L);
    lua_pushstring(L, "type");
    lua_pushstring(L, "request");
    lua_rawset(L,-3);

    lua_pushstring(L, "method");
    lua_pushstring(L, string(s->req.method_string()).c_str());
    lua_rawset(L,-3);

    lua_pushstring(L, "target");
    lua_pushstring(L, string(s->req.target()).c_str());
    lua_rawset(L,-3);

    lua_pushstring(L, "is_upgrade");
    lua_pushboolean(L, websocket::is_upgrade(s->req));
    lua_rawset(L,-3);

    int len = s->req.body().size();
    //cout << "HTTP request has size " << len << endl;
    lua_pushstring(L, "data");
    lua_pushlstring(L, s->req.body().c_str(), len);
    lua_rawset(L,-3);

    luaL_pushptr(L, s, "ezhttp");

    //Subtle: pushptr will call ref() if needed. We
    //still need to call put() to release the references
    //held by this handler call
    s->put();
}

int ezserver_accept(lua_State *L) {
    ezserver *s = luaL_checkptr<ezserver> (L, 1, "ezserver");

    //cout << "Called accept" << endl;
    
    auto handler = bind(
        accept_handler,
        L, s->ref(), /*extend s's lifetime until the accept handler put()s it*/
        _1, _2
    );

    s->acc.async_accept(handler);

    return 0;
}

int ezserver_next_event(lua_State *L) {
    //cout << "In next_event" << endl;
    ezserver *s = luaL_checkptr<ezserver> (L, 1, "ezserver");

    //All handlers may push up to two things to the stack
    lua_checkstack(L, 2);

    int last_top = lua_gettop(L);
    do {
        int rc = s->ctx.run_one();
        //cout << "Unblocked" << endl;
        if (rc == 0) s->ctx.restart(); //Is this correct????????
    } while (lua_gettop(L) == last_top);

    //Let the compiler optimize if it wants
    return lua_gettop(L) - last_top;
}

int ezserver_tostring(lua_State *L) {
    ezserver *s = luaL_checkptr<ezserver> (L, 1, "ezserver");
    
    ostringstream os;
    os << "ezserver listening on port " << s->port;

    string str = os.str();
    lua_pushlstring(L, str.c_str(), str.size());

    return 1;
}

int ezserver_gc(lua_State *L) {
    ezserver *s = luaL_checkptr<ezserver> (L, 1, "ezserver");

    s->put();

    return 0;
}

int ezserv_start_server(lua_State *L) {
    int port = 80;
    if (lua_gettop(L) > 0) {
        port = luaL_checkinteger(L,-1);
    }

    //The refcounted base will start with refcount = 1
    ezserver *ret = new ezserver;
    
    errcode ec;
    ret->port = port;
    ret->acc.set_option(
        boost::asio::socket_base::reuse_address(true),
        ec
    );
    tcp::endpoint endpoint(tcp::v4(), port);
    if (!ec) ret->acc.bind(endpoint, ec);
    if (!ec) ret->acc.listen(5, ec);

    if (ec) {
        ret->put();
        lua_pushstring(L, string(ec.message()).c_str());
        lua_error(L);
        return 1; //Should never get here
    }
    
    luaL_pushptr(L, ret, "ezserver");

    //Subtle: pushptr will call ref() if needed. We
    //need to call put() to avoid a memory leak (the
    //ezserver is constructed with a refcount of 1)
    ret->put();
    
    return 1;
}

//Optional third argument to enable an "acknowledge" event
int ezhttp_send(lua_State *L) {
    ezhttp *s = luaL_checkptr<ezhttp>(L, 1, "ezhttp");
    if (s->rsp_vld) {
        luaL_error(L, "Can not have more than one response in flight per HTTP session");
    }

    //cout << "In ezhttp_send" << endl;

    //Common response settings
    s->rsp.version(11);
    s->rsp.set(http::field::server, "ezserver");
    
    bool en_ack = lua_toboolean(L,3);

    char *data = nullptr;
    
    switch (lua_type(L,2)) {
    //For sending raw data
    case LUA_TSTRING: {
        size_t len = 0;
        char const *str = lua_tolstring(L, 2, &len);
        //cout << "Sending: [" << endl;
        //cout << str << "]" << endl;
        //cout << "(of length " << len << endl;
        data = new char[len];
        memcpy(data, str, len);

        s->rsp.result(http::status::ok);
        s->rsp.body().data = data;
        s->rsp.body().size = len;
        s->rsp.body().more = false;
        break;
    }
    //For sending error codes. TODO: add support for nonempty body
    case LUA_TNUMBER: {
        http::status status = static_cast<http::status>(
            luaL_checkinteger(L,2)
        );
        s->rsp.result(status);
        s->rsp.body().data = nullptr;
        s->rsp.body().size = 0;
        s->rsp.body().more = false;
        break;
    }
    //TODO: support other types????
    default:
        luaL_error(L, "This type is not supported for sending data on HTTP");
    }

    s->rsp.prepare_payload();
    
    auto handler = bind(
        http_send_handler,
        L, s->ref(), data, en_ack,
        _1, _2
    );
    
    http::async_write(s->sock, s->rsp, handler);
    s->rsp_vld = true;

    return 0;
}

int ezhttp_recv(lua_State *L) {
    ezhttp *s = luaL_checkptr<ezhttp>(L, 1, "ezhttp");

    //cout << "In ezhttp_recv" << endl;
    
    auto handler = bind(
        http_recv_handler,
        L, s->ref(),
        _1,_2
    );

    //cout << "Calling async_read" << endl;
    http::async_read(s->sock, s->req_buf, s->req, handler);

    return 0;
}

int ezhttp_upgrade(lua_State *L) {
    ezhttp *h = luaL_checkptr<ezhttp>(L, 1, "ezhttp");

    if (!websocket::is_upgrade(h->req)) {
        luaL_error("Cannot upgrade an ezhttp unless it has first received an upgrade request");
    }

    ezwebsock *s = new ezwebsock(h->sock);

    auto handler = bind(
        ws_accept_handler,
        L, h->ref(), s->ref(),
        _1
    );

    s->sock.async_accept(h->req, handler);

    //Subtle: this method was called from an ezhttp, which
    //will still be in scope (in Lua) after we return. So 
    //we don't call h->put(). (Note that we did call ref() 
    //in the bind to the handler because the ezhttp needs 
    //to stay alive at least until the handler finishes). 
    //However, because this function does not return an 
    //ezwebsock, we need to release the ezwebsock that we 
    //constructed (remember that the constructor initializes 
    //the refcount to 1).
    s->put();
}

int ezhttp_dbg_refcount(lua_State *L) {
    ezhttp *s = luaL_checkptr<ezhttp>(L, 1, "ezhttp");
    cout << *(s->refcount) << endl;
    return 0;
}

int ezhttp_gc(lua_State *L) {
    ezhttp *s = luaL_checkptr<ezhttp>(L, 1, "ezhttp");
    //cout << "I am here" << endl;
    s->put();
    return 0;
}

int ezwebsock_send(lua_State *L) {
    ezwebsock *s = luaL_checkptr<ezwebsock>(L, 1, "ezwebsock");
    switch (lua_type(L,2)) {
    //For sending raw data
    case LUA_TSTRING: {
        size_t len = 0;
        char const *str = lua_tolstring(L, 2, &len);
        //cout << "Sending: [" << endl;
        //cout << str << "]" << endl;
        //cout << "(of length " << len << endl;
        data = new char[len];
        memcpy(data, str, len);
        
        break;
    }
    //TODO: use same number trick to allow server to send error codes
    //TODO: support other types????
    default:
        luaL_error(L, "This type is not supported for sending data on a websocket");
    }
    
}

int ezwebsock_gc(lua_State *L) {
    ezwebsock *s = luaL_checkptr<ezwebsock>(L, 1, "ezwebsock");
    s->put();
    return 0;
}

int unimplemented(lua_State *L) {
    luaL_error(L, "Sorry, this is not implemented!");
    return 0;
}

extern "C"
int luaopen_ezserv(lua_State *L) {
    luaL_newmetatable(L, "ezserver");

    lua_pushstring(L, "__index");
    lua_pushvalue(L, -2);
    lua_rawset(L, -3); //Set __index to point at ezserver table
    
    lua_pushstring(L, "accept");
    lua_pushcfunction(L, &ezserver_accept);
    lua_rawset(L, -3); //Set ezserver.accept
    
    lua_pushstring(L, "next_event");
    lua_pushcfunction(L, &ezserver_next_event);
    lua_rawset(L, -3); //Set ezserver.next_event

    lua_pushstring(L, "__tostring");
    lua_pushcfunction(L, &ezserver_tostring);
    lua_rawset(L, -3); //Set ezserver.__tostring

    lua_pushstring(L, "__gc");
    lua_pushcfunction(L, &ezserver_gc);
    lua_rawset(L, -3); //Set ezserver.__gc
    
    lua_pop(L, 1); //Done with ezserver type



    
    
    luaL_newmetatable(L, "ezhttp");

    lua_pushstring(L, "__index");
    lua_pushvalue(L, -2);
    lua_rawset(L, -3); //Set __index to point at ezhttp table

    lua_pushstring(L, "send");
    lua_pushcfunction(L, ezhttp_send);
    lua_rawset(L, -3); //Set ezhttp.send

    lua_pushstring(L, "recv");
    lua_pushcfunction(L, ezhttp_recv);
    lua_rawset(L, -3); //Set ezhttp.recv
    
    lua_pushstring(L, "upgrade");
    lua_pushcfunction(L, ezhttp_upgrade);
    lua_rawset(L, -3); //Set ezhttp.upgrade

    lua_pushstring(L, "__gc");
    lua_pushcfunction(L, &ezhttp_gc);
    lua_rawset(L, -3); //Set ezhttp.__gc

    lua_pop(L, 1); //Done with ezhttp type


    
    
    luaL_newmetatable(L, "ezwebsock");

    lua_pushstring(L, "__index");
    lua_pushvalue(L, -2);
    lua_rawset(L, -3); //Set __index to point at ezwebsock table

    lua_pushstring(L, "send");
    lua_pushcfunction(L, &unimplemented);
    lua_rawset(L, -3); //Set ezwebsock.send

    lua_pushstring(L, "recv");
    lua_pushcfunction(L, &unimplemented);
    lua_rawset(L, -3); //Set ezwebsock.recv

    lua_pushstring(L, "__gc");
    lua_pushcfunction(L, &ezwebsock_gc);
    lua_rawset(L, -3); //Set ezwebsock.__gc

    lua_pop(L, 1); //Done with ezwebsock type



    
    lua_newtable(L); //Make table that will contain the free functions
                     //and constants
    lua_pushstring(L, "start_server");
    lua_pushcfunction(L, &ezserv_start_server);
    lua_rawset(L, -3);

    //Add in the table of constants
    lua_pushstring(L, "http");
    lua_newtable(L);
    #define mkcode(code)\
        lua_pushlstring(L, #code, sizeof(#code) - 1); \
        lua_pushinteger(L, static_cast<int>(http::status::code)); \
        lua_rawset(L,-3)
    mkcode(ok);
    mkcode(not_found);
    mkcode(method_not_allowed);
    mkcode(bad_request);
    mkcode(not_implemented);
    mkcode(unauthorized);
    mkcode(forbidden);
    mkcode(internal_server_error);
    mkcode(service_unavailable);
    lua_rawset(L,-3);

    //Done setting up library table, but we want to add
    //a few things to the registry before returning
    
    //Idea: anytime we want to give a pointer
    //to one of our types in a userdata, we
    //should see if we already created a userdata

    //Use a pointer to one of our functions to avoid
    //collisions in the registry
    lua_pushlightuserdata(L, reinterpret_cast<void*>(&luaopen_ezserv));
    lua_newtable(L);
        lua_newtable(L); //metatable so we can have weak refs
        lua_pushstring(L,"__mode");
        lua_pushstring(L,"kv");
        lua_rawset(L,-3);
    lua_setmetatable(L,-2);
    lua_settable(L,LUA_REGISTRYINDEX);
    
    return 1;
}