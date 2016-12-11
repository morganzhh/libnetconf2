/**
 * \file session_p.h
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \author Michal Vasko <mvasko@cesnet.cz>
 * \brief libnetconf2 session manipulation
 *
 * Copyright (c) 2015 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#ifndef NC_SESSION_PRIVATE_H_
#define NC_SESSION_PRIVATE_H_

#include <stdint.h>
#include <pthread.h>

#include <libyang/libyang.h>

#include "libnetconf.h"
#include "netconf.h"
#include "session.h"
#include "messages_client.h"

#ifdef NC_ENABLED_SSH

#   include <libssh/libssh.h>
#   include <libssh/callbacks.h>
#   include <libssh/server.h>

/* seconds */
#   define NC_SSH_TIMEOUT 10
/* number of all supported authentication methods */
#   define NC_SSH_AUTH_COUNT 3

/* ACCESS unlocked */
struct nc_client_ssh_opts {
    /* SSH authentication method preferences */
    struct {
        NC_SSH_AUTH_TYPE type;
        int16_t value;
    } auth_pref[NC_SSH_AUTH_COUNT];

    /* SSH key pairs */
    struct {
        char *pubkey_path;
        char *privkey_path;
        int8_t privkey_crypt;
    } *keys;
    uint16_t key_count;

    /* SSH authentication callbacks */
    int (*auth_hostkey_check)(const char *hostname, ssh_session session);
    char *(*auth_password)(const char *, const char *);
    char *(*auth_interactive)(const char *, const char *, const char *, int);
    char *(*auth_privkey_passphrase)(const char *);

    char *username;
};

/* ACCESS locked, separate locks */
struct nc_server_ssh_opts {
    /* SSH bind options */
    const char **hostkeys;
    uint8_t hostkey_count;
    const char *banner;

    struct {
        const char *path;
        const char *username;
    } *authkeys;
    uint16_t authkey_count;

    int auth_methods;
    uint16_t auth_attempts;
    uint16_t auth_timeout;
};

#endif /* NC_ENABLED_SSH */

#ifdef NC_ENABLED_TLS

#   include <openssl/bio.h>
#   include <openssl/ssl.h>

/* ACCESS unlocked */
struct nc_client_tls_opts {
    char *cert_path;
    char *key_path;
    char *ca_file;
    char *ca_dir;
    int8_t tls_ctx_change;
    SSL_CTX *tls_ctx;

    char *crl_file;
    char *crl_dir;
    int8_t crl_store_change;
    X509_STORE *crl_store;
};

/* ACCESS locked, separate locks */
struct nc_server_tls_opts {
    EVP_PKEY *server_key;
    X509 *server_cert;
    struct nc_cert {
        const char *name;
        X509 *cert;
    } *trusted_certs;
    uint16_t trusted_cert_count;
    const char *trusted_ca_file;
    const char *trusted_ca_dir;
    X509_STORE *crl_store;

    struct nc_ctn {
        uint32_t id;
        const char *fingerprint;
        NC_TLS_CTN_MAPTYPE map_type;
        const char *name;
        struct nc_ctn *next;
    } *ctn;
};

#endif /* NC_ENABLED_TLS */

/* ACCESS unlocked */
struct nc_client_opts {
    char *schema_searchpath;

    struct nc_bind {
        const char *address;
        uint16_t port;
        int sock;
        NC_TRANSPORT_IMPL ti;
    } *ch_binds;
    uint16_t ch_bind_count;
};

struct nc_server_opts {
    /* ACCESS unlocked (dictionary locked internally in libyang) */
    struct ly_ctx *ctx;

    /* ACCESS unlocked */
    NC_WD_MODE wd_basic_mode;
    int wd_also_supported;
    int interleave_capab;

    /* ACCESS unlocked */
    uint16_t hello_timeout;
    uint16_t idle_timeout;

    /* ACCESS locked, add/remove binds/endpts - WRITE lock endpt_array_lock
     *                modify binds/endpts - READ lock endpt_array_lock + endpt_lock */
    struct nc_bind *binds;
    struct nc_endpt {
        const char *name;
#ifdef NC_ENABLED_SSH
        struct nc_server_ssh_opts *ssh_opts;
#endif
#ifdef NC_ENABLED_TLS
        struct nc_server_tls_opts *tls_opts;
#endif
        pthread_mutex_t endpt_lock;
    } *endpts;
    uint16_t endpt_count;
    pthread_rwlock_t endpt_array_lock;

    /* ACCESS locked with sid_lock */
    uint32_t new_session_id;
    pthread_spinlock_t sid_lock;
};

/**
 * Sleep time in msec to wait between nc_recv_notif() calls.
 */
#define NC_CLIENT_NOTIF_THREAD_SLEEP 10000

/**
 * Timeout in msec for transport-related data to arrive (ssh_handle_key_exchange(), SSL_accept(), SSL_connect()).
 */
#define NC_TRANSPORT_TIMEOUT 500

/**
 * Number of sockets kept waiting to be accepted.
 */
#define NC_REVERSE_QUEUE 1

/**
 * @brief Type of the session
 */
typedef enum {
    NC_CLIENT,        /**< client side */
    NC_SERVER         /**< server side */
} NC_SIDE;

/**
 * @brief Enumeration of the supported NETCONF protocol versions
 */
typedef enum {
    NC_VERSION_10 = 0,  /**< NETCONF 1.0 - RFC 4741, 4742 */
    NC_VERSION_11 = 1   /**< NETCONF 1.1 - RFC 6241, 6242 */
} NC_VERSION;

#define NC_VERSION_10_ENDTAG "]]>]]>"
#define NC_VERSION_10_ENDTAG_LEN 6

/**
 * @brief Container to serialize PRC messages
 */
struct nc_msg_cont {
    struct lyxml_elem *msg;
    struct nc_msg_cont *next;
};

/**
 * @brief NETCONF session structure
 */
struct nc_session {
    NC_STATUS status;            /**< status of the session */
    NC_SESSION_TERM_REASON term_reason; /**< reason of termination, if status is NC_STATUS_INVALID */
    NC_SIDE side;                /**< side of the session: client or server */

    /* NETCONF data */
    uint32_t id;                 /**< NETCONF session ID (session-id-type) */
    NC_VERSION version;          /**< NETCONF protocol version */

    /* Transport implementation */
    NC_TRANSPORT_IMPL ti_type;   /**< transport implementation type to select items from ti union */
    pthread_mutex_t *ti_lock;    /**< lock to access ti. Note that in case of libssh TI, it can be shared with other
                                      NETCONF sessions on the same SSH session (but different SSH channel) */
    union {
        struct {
            int in;              /**< input file descriptor */
            int out;             /**< output file descriptor */
        } fd;                    /**< NC_TI_FD transport implementation structure */
#ifdef NC_ENABLED_SSH
        struct {
            ssh_channel channel;
            ssh_session session;
            struct nc_session *next; /**< pointer to the next NETCONF session on the same
                                          SSH session, but different SSH channel. If no such session exists, it is NULL.
                                          otherwise there is a ring list of the NETCONF sessions */
        } libssh;
#endif
#ifdef NC_ENABLED_TLS
        SSL *tls;
#endif
    } ti;                          /**< transport implementation data */
    const char *username;
    const char *host;
    uint16_t port;

    /* other */
    struct ly_ctx *ctx;            /**< libyang context of the session */
    void *data;                    /**< arbitrary user data */
    uint8_t flags;                 /**< various flags of the session - TODO combine with status and/or side */
#define NC_SESSION_SHAREDCTX 0x01
#define NC_SESSION_CALLHOME 0x02

    /* client side only data */
    uint64_t msgid;
    const char **cpblts;           /**< list of server's capabilities on client side */
    struct nc_msg_cont *replies;   /**< queue for RPC replies received instead of notifications */
    struct nc_msg_cont *notifs;    /**< queue for notifications received instead of RPC reply */
    volatile pthread_t *ntf_tid;   /**< running notifications receiving thread */

    /* some server modules failed to load so the data from them will be ignored - not use strict flag for parsing */
#   define NC_SESSION_CLIENT_NOT_STRICT 0x40

    /* server side only data */
    time_t session_start;          /**< time the session was created */
    time_t last_rpc;               /**< time the last RPC was received on this session */

#ifdef NC_ENABLED_SSH
    /* SSH session authenticated */
#   define NC_SESSION_SSH_AUTHENTICATED 0x04
    /* netconf subsystem requested */
#   define NC_SESSION_SSH_SUBSYS_NETCONF 0x08
    /* new SSH message arrived */
#   define NC_SESSION_SSH_NEW_MSG 0x10
    /* this session is passed to nc_sshcb_msg() */
#   define NC_SESSION_SSH_MSG_CB 0x20

    uint16_t ssh_auth_attempts;    /**< number of failed SSH authentication attempts */
#endif
#ifdef NC_ENABLED_TLS
    X509 *tls_cert;                /**< TLS client certificate it used for authentication */
#endif
};

#define NC_PS_QUEUE_SIZE 6

/* ACCESS locked */
struct nc_pollsession {
    struct pollfd *pfds;
    struct nc_session **sessions;
    uint16_t session_count;

    pthread_cond_t cond;
    pthread_mutex_t lock;
    uint8_t queue[NC_PS_QUEUE_SIZE]; /**< round buffer, queue is empty when queue_len == 0 */
    uint8_t queue_begin;             /**< queue starts on queue[queue_begin] */
    uint8_t queue_len;               /**< queue ends on queue[queue_begin + queue_len - 1] */
};

struct nc_ntf_thread_arg {
    struct nc_session *session;
    void (*notif_clb)(struct nc_session *session, const struct nc_notif *notif);
};

void *nc_realloc(void *ptr, size_t size);

NC_MSG_TYPE nc_send_msg(struct nc_session *session, struct lyd_node *op);

#ifndef HAVE_PTHREAD_MUTEX_TIMEDLOCK
int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abstime);
#endif

int nc_gettimespec(struct timespec *ts);

int nc_timedlock(pthread_mutex_t *lock, int timeout, const char *func);

int nc_ps_lock(struct nc_pollsession *ps, uint8_t *id, const char *func);

int nc_ps_unlock(struct nc_pollsession *ps, uint8_t id, const char *func);

/**
 * @brief Fill libyang context in \p session. Context models are based on the stored session
 *        capabilities. If the server does not support \<get-schema\>, the models are searched
 *        for in the directory set using nc_client_schema_searchpath().
 *
 * @param[in] session Session to create the context for.
 * @return 0 on success, 1 on some missing schemas, -1 on error.
 */
int nc_ctx_check_and_fill(struct nc_session *session);

/**
 * @brief Perform NETCONF handshake on \p session.
 *
 * @param[in] session NETCONF session to use.
 * @return NC_MSG_HELLO on success, NC_MSG_BAD_HELLO on client \<hello\> message parsing fail
 * (server-side only), NC_MSG_WOULDBLOCK on timeout, NC_MSG_ERROR on other error.
 */
NC_MSG_TYPE nc_handshake(struct nc_session *session);

/**
 * @brief Create a socket connection.
 *
 * @param[in] host Hostname to connect to.
 * @param[in] port Port to connect on.
 * @return Connected socket or -1 on error.
 */
int nc_sock_connect(const char *host, uint16_t port);

/**
 * @brief Accept a new socket connection.
 *
 * @param[in] sock Listening socket.
 * @param[in] timeout Timeout in milliseconds.
 * @param[out] peer_host Host the new connection was initiated from. Can be NULL.
 * @param[out] peer_port Port the new connection is connected on. Can be NULL.
 * @return Connected socket with the new connection, -1 on error.
 */
int nc_sock_accept(int sock, int timeout, char **peer_host, uint16_t *peer_port);

/**
 * @brief Create a listening socket.
 *
 * @param[in] address IP address to listen on.
 * @param[in] port Port to listen on.
 * @return Listening socket, -1 on error.
 */
int nc_sock_listen(const char *address, uint16_t port);

/**
 * @brief Accept a new connection on a listening socket.
 *
 * @param[in] binds Structure with the listening sockets.
 * @param[in] bind_count Number of \p binds.
 * @param[in] timeout Timeout for accepting.
 * @param[out] host Host of the remote peer. Can be NULL.
 * @param[out] port Port of the new connection. Can be NULL.
 * @param[out] idx Index of the bind that was accepted. Can be NULL.
 * @return Accepted socket of the new connection, -1 on error.
 */
int nc_sock_accept_binds(struct nc_bind *binds, uint16_t bind_count, int timeout, char **host, uint16_t *port, uint16_t *idx);

/**
 * @brief Change an existing endpoint bind.
 *
 * On error the listening socket is left untouched.
 *
 * @param[in] endpt_name Name of the endpoint.
 * @param[in] address New address. NULL if \p port.
 * @param[in] port New port. NULL if \p address.
 * @param[in] ti Expected transport.
 * @return 0 on success, -1 on error.
 */
int nc_server_endpt_set_address_port(const char *endpt_name, const char *address, uint16_t port, NC_TRANSPORT_IMPL ti);

/**
 * @brief Lock endpoint structures for reading and the specific endpoint.
 *
 * @param[in] name Name of the endpoint.
 * @param[out] idx Index of the endpoint. Optional.
 * @return Endpoint structure.
 */
struct nc_endpt *nc_server_endpt_lock(const char *name, uint16_t *idx);

/**
 * @brief Unlock endpoint strcutures and the specific endpoint.
 *
 * @param[in] endpt Locked endpoint structure.
 */
void nc_server_endpt_unlock(struct nc_endpt *endpt);

/**
 * @brief Add a client Call Home bind, listen on it.
 *
 * @param[in] address Address to bind to.
 * @param[in] port to bind to.
 * @param[in] ti Expected transport.
 * @return 0 on success, -1 on error.
 */
int nc_client_ch_add_bind_listen(const char *address, uint16_t port, NC_TRANSPORT_IMPL ti);

/**
 * @brief Remove a client Call Home bind, stop listening on it.
 *
 * @param[in] address Address of the bind. NULL matches any address.
 * @param[in] port Port of the bind. 0 matches all ports.
 * @param[in] ti Expected transport of the bind. 0 matches any.
 * @return 0 on success, -1 on no matches found.
 */
int nc_client_ch_del_bind(const char *address, uint16_t port, NC_TRANSPORT_IMPL ti);

/**
 * @brief Connect to a listening NETCONF client using Call Home.
 *
 * @param[in] host Hostname to connect to.
 * @param[in] port Port to connect to.
 * @param[in] ti Transport fo the connection.
 * @param[out] session New Call Home session.
 * @return NC_MSG_HELLO on success, NC_MSG_BAD_HELLO on client \<hello\> message
 *         parsing fail, NC_MSG_WOULDBLOCK on timeout, NC_MSG_ERROR on other errors.
 */
NC_MSG_TYPE nc_connect_callhome(const char *host, uint16_t port, NC_TRANSPORT_IMPL ti, struct nc_session **session);

void nc_init(void);

void nc_destroy(void);

#ifdef NC_ENABLED_SSH

/**
 * @brief Accept a server Call Home connection on a socket.
 *
 * @param[in] sock Socket with a new connection.
 * @param[in] host Hostname of the server.
 * @param[in] port Port of the server.
 * @param[in] ctx Context for the session. Can be NULL.
 * @param[in] timeout Transport operations timeout in msec.
 * @return New session, NULL on error.
 */
struct nc_session *nc_accept_callhome_ssh_sock(int sock, const char *host, uint16_t port, struct ly_ctx *ctx, int timeout);

/**
 * @brief Establish SSH transport on a socket.
 *
 * @param[in] session Session structure of the new connection.
 * @param[in] sock Socket of the new connection.
 * @param[in] timeout Transport operations timeout in msec (not SSH authentication one).
 * @return 1 on success, 0 on timeout, -1 on error.
 */
int nc_accept_ssh_session(struct nc_session *session, int sock, int timeout);

/**
 * @brief Callback called when a new SSH message is received.
 *
 * @param[in] sshsession SSH session the message arrived on.
 * @param[in] msg SSH message itself.
 * @param[in] data NETCONF session running on \p sshsession.
 * @return 0 if the message was handled, 1 if it is left up to libssh.
 */
int nc_sshcb_msg(ssh_session sshsession, ssh_message msg, void *data);

/**
 * @brief Inspect what exactly happened if a SSH session socket poll
 * returned POLLIN.
 *
 * @param[in] session NETCONF session communicating on the socket.
 * @param[in,out] timeout Timeout for locking ti_lock.
 * @return NC_PSPOLL_TIMEOUT,
 *         NC_PSPOLL_RPC (has new data),
 *         NC_PSPOLL_PENDING (other channel has data),
 *         NC_PSPOLL_SESSION_TERM | NC_PSPOLL_SESSION_ERROR,
 *         NC_PSPOLL_SSH_MSG,
 *         NC_PSPOLL_SSH_CHANNEL,
 *         NC_PSPOLL_ERROR.
 */
int nc_ssh_pollin(struct nc_session *session, int timeout);

void nc_server_ssh_clear_opts(struct nc_server_ssh_opts *opts);

void nc_client_ssh_destroy_opts(void);

#endif /* NC_ENABLED_SSH */

#ifdef NC_ENABLED_TLS

struct nc_session *nc_accept_callhome_tls_sock(int sock, const char *host, uint16_t port, struct ly_ctx *ctx, int timeout);

/**
 * @brief Establish TLS transport on a socket.
 *
 * @param[in] session Session structure of the new connection.
 * @param[in] sock Socket of the new connection.
 * @param[in] timeout Transport operations timeout in msec.
 * @return 1 on success, 0 on timeout, -1 on error.
 */
int nc_accept_tls_session(struct nc_session *session, int sock, int timeout);

void nc_server_tls_clear_opts(struct nc_server_tls_opts *opts);

void nc_client_tls_destroy_opts(void);

#endif /* NC_ENABLED_TLS */

/**
 * Functions
 * - io.c
 */

/**
 * @brief Read message from the wire.
 *
 * Accepts hello, rpc, rpc-reply and notification. Received string is transformed into
 * libyang XML tree and the message type is detected from the top level element.
 *
 * @param[in] session NETCONF session from which the message is being read.
 * @param[in] timeout Timeout in milliseconds. Negative value means infinite timeout,
 *            zero value causes to return immediately.
 * @param[out] data XML tree built from the read data.
 * @return Type of the read message. #NC_MSG_WOULDBLOCK is returned if timeout is positive
 * (or zero) value and it passed out without any data on the wire. #NC_MSG_ERROR is
 * returned on error and #NC_MSG_NONE is never returned by this function.
 */
NC_MSG_TYPE nc_read_msg_poll(struct nc_session* session, int timeout, struct lyxml_elem **data);

/**
 * @brief Read message from the wire.
 *
 * Accepts hello, rpc, rpc-reply and notification. Received string is transformed into
 * libyang XML tree and the message type is detected from the top level element.
 *
 * @param[in] session NETCONF session from which the message is being read.
 * @param[out] data XML tree built from the read data.
 * @return Type of the read message. #NC_MSG_WOULDBLOCK is returned if timeout is positive
 * (or zero) value and it passed out without any data on the wire. #NC_MSG_ERROR is
 * returned on error and #NC_MSG_NONE is never returned by this function.
 */
NC_MSG_TYPE nc_read_msg(struct nc_session* session, struct lyxml_elem **data);

/**
 * @brief Write message into wire.
 *
 * @param[in] session NETCONF session to which the message will be written.
 * @param[in] type Type of the message to write. According to the type, the
 * specific additional parameters are required or accepted:
 * - #NC_MSG_RPC
 *   - `struct lyd_node *op;` - operation (content of the \<rpc/\> to be sent. Required parameter.
 *   - `const char *attrs;` - additional attributes to be added into the \<rpc/\> element.
 *     Required parameter.
 *     `message-id` attribute is added automatically and default namespace is set to #NC_NS_BASE.
 *     Optional parameter.
 * - #NC_MSG_REPLY
 *   - `struct lyxml_node *rpc_elem;` - root of the RPC object to reply to. Required parameter.
 *   - `struct nc_server_reply *reply;` - RPC reply. Required parameter.
 * - #NC_MSG_NOTIF
 *   - TODO: content
 * @return 0 on success
 */
int nc_write_msg(struct nc_session *session, NC_MSG_TYPE type, ...);

/**
 * @brief Check whether a session is still connected (on transport layer).
 *
 * @param[in] session Session to check.
 * @return 1 if connected, 0 if not.
 */
int nc_session_is_connected(struct nc_session *session);

#endif /* NC_SESSION_PRIVATE_H_ */
