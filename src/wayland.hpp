#pragma once

#include <wayland-client.h>

#include "util/bindings.hpp"

namespace wayland {

  /** \class event_queue
   *
   * \brief A queue for \ref proxy object events.
   *
   * Event queues allows the events on a display to be handled in a thread-safe
   * manner. See \ref display for details.
   *
   */
  struct EventQueue {
    CLOTH_BIND_BASE(EventQueue, wl_event_queue);

    void destroy() noexcept
    {
      wl_event_queue_destroy(base());
    }
  };


  /** \class proxy
   *
   * \brief Represents a protocol object on the client side.
   *
   * A proxy acts as a lient side proxy to an object existing in the
   * compositor. The proxy is responsible for converting requests made by the
   * clients with \ref proxy_marshal() into Wayland's wire format. Events
   * coming from the compositor are also handled by the proxy, which will in
   * turn call the handler set with \ref proxy_add_listener().
   *
   * \note With the exception of function \ref proxy_set_queue(), functions
   * accessing a proxy are not normally used by client code. Clients
   * should normally use the higher level interface generated by the scanner to
   * interact with compositor objects.
   *
   */
  struct Proxy {
    CLOTH_BIND_BASE(Proxy, wl_proxy);

    template<typename... Args>
    void marshal(uint32_t opcode, Args... args) noexcept
    {
      return wl_proxy_marshal(base(), opcode, args...);
    }

    void marshal_array(uint32_t opcode, union wl_argument* args) noexcept
    {
      return wl_proxy_marshal_array(base(), opcode, args);
    }

    Proxy create(const struct wl_interface* interface) noexcept
    {
      return Proxy{wl_proxy_create(base(), interface)};
    }

    static void* create_wrapper(void* proxy) noexcept
    {
      return wl_proxy_create_wrapper(proxy);
    }

    static void wrapper_destroy(void* proxy_wrapper) noexcept
    {
      wl_proxy_wrapper_destroy(proxy_wrapper);
    }

    template<typename... Args>
    Proxy marshal_constructor(uint32_t opcode,
                              const struct interface* interface,
                              Args... args) noexcept
    {
      return Proxy{wl_proxy_marshal_constructor(base(), opcode, interface, args...)};
    }

    template<typename... Args>
    Proxy marshal_constructor_versioned(uint32_t opcode,
                                        const struct wl_interface* interface,
                                        uint32_t version,
                                        Args... args) noexcept
    {
      return Proxy{
        wl_proxy_marshal_constructor_versioned(base(), opcode, interface, version, args...)};
    }

    Proxy marshal_array_constructor(uint32_t opcode,
                                    union wl_argument* args,
                                    const struct wl_interface* interface) noexcept
    {
      return Proxy{wl_proxy_marshal_array_constructor(base(), opcode, args, interface)};
    }

    Proxy marshal_array_constructor_versioned(uint32_t opcode,
                                              union wl_argument* args,
                                              const struct wl_interface* interface,
                                              uint32_t version) noexcept
    {
      return Proxy{
        wl_proxy_marshal_array_constructor_versioned(base(), opcode, args, interface, version)};
    }

    void destroy() noexcept
    {
      return wl_proxy_destroy(base());
    }

    int add_listener(void (**implementation)(void), void* data) noexcept
    {
      return wl_proxy_add_listener(base(), implementation, data);
    }

    const void* get_listener() noexcept
    {
      return wl_proxy_get_listener(base());
    }

    int add_dispatcher(wl_dispatcher_func_t dispatcher_func,
                       const void* dispatcher_data,
                       void* data) noexcept
    {
      return wl_proxy_add_dispatcher(base(), dispatcher_func, dispatcher_data, data);
    }

    void set_user_data(void* user_data) noexcept
    {
      return wl_proxy_set_user_data(base(), user_data);
    }

    void* get_user_data() noexcept
    {
      return wl_proxy_get_user_data(base());
    }

    uint32_t get_version() noexcept
    {
      return wl_proxy_get_version(base());
    }

    uint32_t get_id() noexcept
    {
      return wl_proxy_get_id(base());
    }

    const char* get_class() noexcept
    {
      return wl_proxy_get_class(base());
    }

    void set_queue(EventQueue queue) noexcept
    {
      return wl_proxy_set_queue(base(), queue);
    }
  };

  /** \class display
   *
   * \brief Represents a connection to the compositor and acts as a proxy to
   * the display singleton object.
   *
   * A display object represents a client connection to a Wayland
   * compositor. It is created with either \ref display_connect() or
   * \ref display_connect_to_fd(). A connection is terminated using
   * \ref display_disconnect().
   *
   * A display is also used as the \ref proxy for the display
   * singleton object on the compositor side.
   *
   * A display object handles all the data sent from and to the
   * compositor. When a \ref proxy marshals a request, it will write its wire
   * representation to the display's write buffer. The data is sent to the
   * compositor when the client calls \ref display_flush().
   *
   * Incoming data is handled in two steps: queueing and dispatching. In the
   * queue step, the data coming from the display fd is interpreted and
   * added to a queue. On the dispatch step, the handler for the incoming
   * event set by the client on the corresponding \ref proxy is called.
   *
   * A display has at least one event queue, called the <em>default
   * queue</em>. Clients can create additional event queues with \ref
   * display_create_queue() and assign \ref proxy's to it. Events
   * occurring in a particular proxy are always queued in its assigned queue.
   * A client can ensure that a certain assumption, such as holding a lock
   * or running from a given thread, is true when a proxy event handler is
   * called by assigning that proxy to an event queue and making sure that
   * this queue is only dispatched when the assumption holds.
   *
   * The default queue is dispatched by calling \ref display_dispatch().
   * This will dispatch any events queued on the default queue and attempt
   * to read from the display fd if it's empty. Events read are then queued
   * on the appropriate queues according to the proxy assignment.
   *
   * A user created queue is dispatched with \ref display_dispatch_queue().
   * This function behaves exactly the same as display_dispatch()
   * but it dispatches given queue instead of the default queue.
   *
   * A real world example of event queue usage is Mesa's implementation of
   * eglSwapBuffers() for the Wayland platform. This function might need
   * to block until a frame callback is received, but dispatching the default
   * queue could cause an event handler on the client to start drawing
   * again. This problem is solved using another event queue, so that only
   * the events handled by the EGL code are dispatched during the block.
   *
   * This creates a problem where a thread dispatches a non-default
   * queue, reading all the data from the display fd. If the application
   * would call \em poll(2) after that it would block, even though there
   * might be events queued on the default queue. Those events should be
   * dispatched with \ref display_dispatch_pending() or \ref
   * display_dispatch_queue_pending() before flushing and blocking.
   */
  struct Display {
    CLOTH_BIND_BASE(Display, wl_display);

    void disconnect() noexcept
    {
      return wl_display_disconnect(base());
    }

    int get_fd() noexcept
    {
      return wl_display_get_fd(base());
    }
    int dispatch() noexcept
    {
      return wl_display_dispatch(base());
    }
    int dispatch_queue(struct event_queue* queue) noexcept
    {
      return wl_display_dispatch_queue(base(), queue);
    }
    int dispatch_queue_pending(struct event_queue* queue) noexcept
    {
      return wl_display_dispatch_queue_pending(base(), queue);
    }
    int dispatch_pending() noexcept
    {
      return wl_display_dispatch_pending(base());
    }
    int get_error() noexcept
    {
      return wl_display_get_error(base());
    }

    uint32_t get_protocol_error(const struct interface** interface, uint32_t* id) noexcept
    {
      return wl_display_get_protocol_error(base(), interface, id);
    }

    int flush() noexcept
    {
      return wl_display_flush(base());
    }
    int roundtrip_queue(struct event_queue* queue) noexcept
    {
      return wl_display_roundtrip_queue(base(), queue);
    }
    int roundtrip() noexcept
    {
      return wl_display_roundtrip(base());
    }

    struct event_queue* create_queue() noexcept
    {
      return wl_display_create_queue(base());
    }
    int prepare_read_queue(struct event_queue* queue) noexcept
    {
      return wl_display_prepare_read_queue(base(), queue);
    }
    int prepare_read() noexcept
    {
      return wl_display_prepare_read(base());
    }
    void cancel_read() noexcept
    {
      return wl_display_cancel_read(base());
    }
    int read_events() noexcept
    {
      return wl_display_read_events(base());
    }

    static Display display_connect(const char* name) noexcept
    {
      return wl_display_display_connect(base(), name);
    }

    static Display display_connect_to_fd(int fd) noexcept
    {
      return wl_display_display_connect_to_fd(base(), fd);
    }
  };

  void log_set_handler_client(wl_log_func_t handler);

  struct Registry {
    CLOTH_BIND_BASE(Registry, wl_registry);
  };

  struct Buffer {
    CLOTH_BIND_BASE(Buffer, wl_buffer);
  };
  struct Callback {
    CLOTH_BIND_BASE(Callback, wl_callback);
  };
  struct Compositor {
    CLOTH_BIND_BASE(Compositor, wl_compositor);
  };
  struct DataDevice {
    CLOTH_BIND_BASE(DataDevice, wl_data_device);
  };
  struct DataDeviceManager {
    CLOTH_BIND_BASE(DataDeviceManager, wl_data_device_manager);
  };
  struct DataOffer {
    CLOTH_BIND_BASE(DataOffer, wl_data_offer);
  };
  struct DataSource {
    CLOTH_BIND_BASE(DataSource, wl_data_source);
  };
  struct Keyboard {
    CLOTH_BIND_BASE(Keyboard, wl_keyboard);
  };
  struct Output {
    CLOTH_BIND_BASE(Output, wl_output);
  };
  struct Pointer {
    CLOTH_BIND_BASE(Pointer, wl_pointer);
  };
  struct Region {
    CLOTH_BIND_BASE(Region, wl_region);
  };
  struct Seat {
    CLOTH_BIND_BASE(Seat, wl_seat);
  };
  struct Shell {
    CLOTH_BIND_BASE(Shell, wl_shell);
  };
  struct ShellSurface {
    CLOTH_BIND_BASE(ShellSurface, wl_shell_surface);
  };
  struct Shm {
    CLOTH_BIND_BASE(Shm, wl_shm);
  };
  struct ShmPool {
    CLOTH_BIND_BASE(ShmPool, wl_shm_pool);
  };
  struct Subcompositor {
    CLOTH_BIND_BASE(Subcompositor, wl_subcompositor);
  };
  struct Subsurface {
    CLOTH_BIND_BASE(Subsurface, wl_subsurface);
  };
  struct Surface {
    CLOTH_BIND_BASE(Surface, wl_surface);
  };
  struct Touch {
    CLOTH_BIND_BASE(Touch, wl_touch);
  };

/**
 * @ingroup iface_wl_display
 * global error values
 *
 * These errors are global and can be emitted in response to any
 * server request.
 */
enum struct DisplayError {
	/**
	 * server couldn't find object
	 */
	invalid_object = WL_DISPLAY_ERROR_INVALID_OBJECT,
	/**
	 * method doesn't exist on the specified interface
	 */
	invalid_method = WL_DISPLAY_ERROR_INVALID_METHOD,
	/**
	 * server is out of memory
	 */
	no_memory = WL_DISPLAY_ERROR_NO_MEMORY,
};

/**
 * @ingroup iface_wl_display
 * @struct wl_display_listener
 */
struct wl_display_listener {
	/**
	 * fatal error event
	 *
	 * The error event is sent out when a fatal (non-recoverable)
	 * error has occurred. The object_id argument is the object where
	 * the error occurred, most often in response to a request to that
	 * object. The code identifies the error and is defined by the
	 * object interface. As such, each interface defines its own set of
	 * error codes. The message is a brief description of the error,
	 * for (debugging) convenience.
	 * @param object_id object where the error occurred
	 * @param code error code
	 * @param message error description
	 */
	void (*error)(void *data,
		      struct wl_display *wl_display,
		      void *object_id,
		      uint32_t code,
		      const char *message);
	/**
	 * acknowledge object ID deletion
	 *
	 * This event is used internally by the object ID management
	 * logic. When a client deletes an object, the server will send
	 * this event to acknowledge that it has seen the delete request.
	 * When the client receives this event, it will know that it can
	 * safely reuse the object ID.
	 * @param id deleted object ID
	 */
	void (*delete_id)(void *data,
			  struct wl_display *wl_display,
			  uint32_t id);
};

/**
 * @ingroup iface_wl_display
 */
static inline int
wl_display_add_listener(struct wl_display *wl_display,
			const struct wl_display_listener *listener, void *data);

/** @ingroup iface_wl_display */
static inline void
wl_display_set_user_data(struct wl_display *wl_display, void *user_data);

/** @ingroup iface_wl_display */
static inline void *
wl_display_get_user_data(struct wl_display *wl_display);

static inline uint32_t
wl_display_get_version(struct wl_display *wl_display);

/**
 * @ingroup iface_wl_display
 *
 * The sync request asks the server to emit the 'done' event
 * on the returned wl_callback object.  Since requests are
 * handled in-order and events are delivered in-order, this can
 * be used as a barrier to ensure all previous requests and the
 * resulting events have been handled.
 *
 * The object returned by this request will be destroyed by the
 * compositor after the callback is fired and as such the client must not
 * attempt to use it after that point.
 *
 * The callback_data passed in the callback is the event serial.
 */
static inline struct wl_callback *
wl_display_sync(struct wl_display *wl_display);

/**
 * @ingroup iface_wl_display
 *
 * This request creates a registry object that allows the client
 * to list and bind the global objects available from the
 * compositor.
 *
 * It should be noted that the server side resources consumed in
 * response to a get_registry request can only be released when the
 * client disconnects, not when the client side proxy is destroyed.
 * Therefore, clients should invoke get_registry as infrequently as
 * possible to avoid wasting memory.
 */
static inline struct wl_registry *
wl_display_get_registry(struct wl_display *wl_display);

/**
 * @ingroup iface_wl_registry
 * @struct wl_registry_listener
 */
struct wl_registry_listener {
	/**
	 * announce global object
	 *
	 * Notify the client of global objects.
	 *
	 * The event notifies the client that a global object with the
	 * given name is now available, and it implements the given version
	 * of the given interface.
	 * @param name numeric name of the global object
	 * @param interface interface implemented by the object
	 * @param version interface version
	 */
	void (*global)(void *data,
		       struct wl_registry *wl_registry,
		       uint32_t name,
		       const char *interface,
		       uint32_t version);
	/**
	 * announce removal of global object
	 *
	 * Notify the client of removed global objects.
	 *
	 * This event notifies the client that the global identified by
	 * name is no longer available. If the client bound to the global
	 * using the bind request, the client should now destroy that
	 * object.
	 *
	 * The object remains valid and requests to the object will be
	 * ignored until the client destroys it, to avoid races between the
	 * global going away and a client sending a request to it.
	 * @param name numeric name of the global object
	 */
	void (*global_remove)(void *data,
			      struct wl_registry *wl_registry,
			      uint32_t name);
};

/**
 * @ingroup iface_wl_registry
 */
static inline int
wl_registry_add_listener(struct wl_registry *wl_registry,
			 const struct wl_registry_listener *listener, void *data);

/** @ingroup iface_wl_registry */
static inline void
wl_registry_set_user_data(struct wl_registry *wl_registry, void *user_data);

/** @ingroup iface_wl_registry */
static inline void *
wl_registry_get_user_data(struct wl_registry *wl_registry);

static inline uint32_t
wl_registry_get_version(struct wl_registry *wl_registry);

/** @ingroup iface_wl_registry */
static inline void
wl_registry_destroy(struct wl_registry *wl_registry);

/**
 * @ingroup iface_wl_registry
 *
 * Binds a new, client-created object to the server using the
 * specified name as the identifier.
 */
static inline void *
wl_registry_bind(struct wl_registry *wl_registry, uint32_t name, const struct wl_interface *interface, uint32_t version);

} // namespace wayland