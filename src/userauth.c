/* Copyright (c) 2004, Sara Golemon <sarag@users.sourceforge.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms,
 * with or without modification, are permitted provided
 * that the following conditions are met:
 *
 *   Redistributions of source code must retain the above
 *   copyright notice, this list of conditions and the
 *   following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials
 *   provided with the distribution.
 *
 *   Neither the name of the copyright holder nor the names
 *   of any other contributors may be used to endorse or
 *   promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#include "libssh2_priv.h"
#include <stdio.h>

/* {{{ proto libssh2_userauth_list
 * List authentication methods
 * Will yield successful login if "none" happens to be allowable for this user
 * Not a common configuration for any SSH server though
 * username should be NULL, or a null terminated string
 */
LIBSSH2_API char *libssh2_userauth_list(LIBSSH2_SESSION *session, char *username, int username_len)
{
	unsigned long data_len = username_len + 31; /* packet_type(1) + username_len(4) + service_len(4) + service(14)"ssh-connection" +
												   method_len(4) + method(4)"none" */
	unsigned long methods_len;
	unsigned char *data, *s;

	s = data = LIBSSH2_ALLOC(session, data_len);
	if (!data) {
		libssh2_error(session, LIBSSH2_ERROR_ALLOC, "Unable to allocate memory for userauth_list", 0);
		return NULL;
	}

	*(s++) = SSH_MSG_USERAUTH_REQUEST;
	libssh2_htonu32(s, username_len);				s += 4;
	if (username) {
		memcpy(s, username, username_len);			s += username_len;
	}

	libssh2_htonu32(s, 14);							s += 4;
	memcpy(s, "ssh-connection", 14);				s += 14;

	libssh2_htonu32(s, 4);							s += 4;
	memcpy(s, "none", 4);							s += 4;

	if (libssh2_packet_write(session, data, data_len)) {
		libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND, "Unable to send userauth-none request", 0);
		LIBSSH2_FREE(session, data);
		return NULL;
	}
	LIBSSH2_FREE(session, data);

	while (1) {
		if (libssh2_packet_ask(session, SSH_MSG_USERAUTH_SUCCESS, &data, &data_len, 1) == 0) {
			/* Wow, who'dve thought... */
			LIBSSH2_FREE(session, data);
			session->authenticated = 1;
			return NULL;
		}

		if (libssh2_packet_ask(session, SSH_MSG_USERAUTH_FAILURE, &data, &data_len, 0) == 0) {
			/* What we *actually* wanted to happen */
			break;
		}
		/* TODO: Timeout? */
	}

	methods_len = libssh2_ntohu32(data + 1);
	memcpy(data, data + 5, methods_len);
	data[methods_len] = '\0';

	return data;
}
/* }}} */

/* {{{ libssh2_userauth_authenticated
 * 0 if not yet authenticated
 * non-zero is already authenticated
 */
LIBSSH2_API int libssh2_userauth_authenticated(LIBSSH2_SESSION *session)
{
	return session->authenticated;
}
/* }}} */

/* {{{ libssh2_userauth_password
 * Plain ol' login
 */
LIBSSH2_API int libssh2_userauth_password_ex(LIBSSH2_SESSION *session, char *username, int username_len,
																					  char *password, int password_len,
																					  LIBSSH2_PASSWD_CHANGEREQ_FUNC((*passwd_change_cb)))
{
	unsigned char *data, *s;
	unsigned long data_len = username_len + password_len + 40; /* packet_type(1) + username_len(4) + service_len(4) + service(14)"ssh-connection" + 
																  method_len(4) + method(8)"password" + chgpwdbool(1) + password_len(4) */

	s = data = LIBSSH2_ALLOC(session, data_len);
	if (!data) {
		libssh2_error(session, LIBSSH2_ERROR_ALLOC, "Unable to allocate memory for userauth-password request", 0);
		return -1;
	}

	*(s++) = SSH_MSG_USERAUTH_REQUEST;
	libssh2_htonu32(s, username_len);							s += 4;
	memcpy(s, username, username_len);							s += username_len;

	libssh2_htonu32(s, sizeof("ssh-connection") - 1);			s += 4;
	memcpy(s, "ssh-connection", sizeof("ssh-connection") - 1);	s += sizeof("ssh-connection") - 1;

	libssh2_htonu32(s, sizeof("password") - 1);					s += 4;
	memcpy(s, "password", sizeof("password") - 1);				s += sizeof("password") - 1;

	*s = '\0';													s++;

	libssh2_htonu32(s, password_len);							s += 4;
	memcpy(s, password, password_len);							s += password_len;

	if (libssh2_packet_write(session, data, data_len)) {
		libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND, "Unable to send userauth-password request", 0);
		LIBSSH2_FREE(session, data);
		return -1;
	}
	LIBSSH2_FREE(session, data);

	while (1) {
		if (libssh2_packet_ask(session, SSH_MSG_USERAUTH_SUCCESS, &data, &data_len, 1) == 0) {
			LIBSSH2_FREE(session, data);
			session->authenticated = 1;
			return 0;
		}

		if (libssh2_packet_ask(session, SSH_MSG_USERAUTH_FAILURE, &data, &data_len, 0) == 0) {
			LIBSSH2_FREE(session, data);
			return -1;
		}

		if (libssh2_packet_ask(session, SSH_MSG_USERAUTH_PASSWD_CHANGEREQ, &data, &data_len, 0) == 0) {
			char *newpw = NULL;
			int newpw_len = 0;

			LIBSSH2_FREE(session, data);
			if (passwd_change_cb) {
				passwd_change_cb(session, &newpw, &newpw_len, &session->abstract);
				if (!newpw) {
					libssh2_error(session, LIBSSH2_ERROR_PASSWORD_EXPIRED, "Password expired, and callback failed", 0);
					return -1;
				}
				data_len = username_len + password_len + 44 + newpw_len; /* basic data_len + newpw_len(4) */
				s = data = LIBSSH2_ALLOC(session, data_len);
				if (!data) {
					libssh2_error(session, LIBSSH2_ERROR_ALLOC, "Unable to allocate memory for userauth-password-change request", 0);
					return -1;
				}

				*(s++) = SSH_MSG_USERAUTH_REQUEST;
				libssh2_htonu32(s, username_len);							s += 4;
				memcpy(s, username, username_len);							s += username_len;

				libssh2_htonu32(s, sizeof("ssh-connection") - 1);			s += 4;
				memcpy(s, "ssh-connection", sizeof("ssh-connection") - 1);	s += sizeof("ssh-connection") - 1;

				libssh2_htonu32(s, sizeof("password") - 1);					s += 4;
				memcpy(s, "password", sizeof("password") - 1);				s += sizeof("password") - 1;

				*s = 0xFF;													s++;

				libssh2_htonu32(s, password_len);							s += 4;
				memcpy(s, password, password_len);							s += password_len;

				libssh2_htonu32(s, newpw_len);								s += 4;
				memcpy(s, newpw, newpw_len);								s += newpw_len;

				if (libssh2_packet_write(session, data, data_len)) {
					libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND, "Unable to send userauth-password-change request", 0);
					LIBSSH2_FREE(session, data);
					return -1;
				}
				LIBSSH2_FREE(session, data);
				LIBSSH2_FREE(session, newpw);
				/* TODO: Reset timeout? */
			} else {
				libssh2_error(session, LIBSSH2_ERROR_PASSWORD_EXPIRED, "Password Expired, and no callback specified", 0);
				return -1;
			}
		}
		/* TODO: Timeout? */
	}

	return 0;
}
/* }}} */

/* {{{ libssh2_file_read_publickey
 * Read a public key from an id_???.pub style file
 */
static int libssh2_file_read_publickey(LIBSSH2_SESSION *session, unsigned char **method, unsigned long *method_len,
																 unsigned char **pubkeydata, unsigned long *pubkeydata_len,
																 char *pubkeyfile)
{
	FILE *fd;
	char *pubkey = NULL, c, *sp1, *sp2, *tmp;
	int pubkey_len = 0, tmp_len;

	/* Read Public Key */
	fd = fopen(pubkeyfile, "r");
	if (!fd) {
		libssh2_error(session, LIBSSH2_ERROR_FILE, "Unable to open public key file", 0);
		return -1;
	}
	while (!feof(fd) && (c = fgetc(fd)) != '\r' && c != '\n')	pubkey_len++;
	rewind(fd);

	if (pubkey_len <= 1) {
		libssh2_error(session, LIBSSH2_ERROR_FILE, "Invalid data in public key file", 0);
		fclose(fd);
		return -1;
	}

	pubkey = LIBSSH2_ALLOC(session, pubkey_len);
	if (!pubkey) {
		libssh2_error(session, LIBSSH2_ERROR_ALLOC, "Unable to allocate memory for public key data", 0);
		fclose(fd);
		return -1;
	}
	if (fread(pubkey, 1, pubkey_len, fd) != pubkey_len) {
		libssh2_error(session, LIBSSH2_ERROR_FILE, "Unable to read public key from file", 0);
		LIBSSH2_FREE(session, pubkey);
		fclose(fd);
		return -1;
	}
	fclose(fd);
	while (pubkey_len && (pubkey[pubkey_len-1] == '\r' || pubkey[pubkey_len-1] == '\n')) pubkey_len--;

	if (!pubkey_len) {
		libssh2_error(session, LIBSSH2_ERROR_FILE, "Missing public key data", 0);
		LIBSSH2_FREE(session, pubkey);
		return -1;
	}

	if ((sp1 = memchr(pubkey, ' ', pubkey_len)) == NULL) {
		libssh2_error(session, LIBSSH2_ERROR_FILE, "Invalid public key data", 0);
		LIBSSH2_FREE(session, pubkey);
		return -1;
	}
	/* Wasting some bytes here (okay, more than some),
	 * but since it's likely to be freed soon anyway, 
	 * we'll just avoid the extra free/alloc and call it a wash */
	*method = pubkey;
	*method_len = sp1 - pubkey;

	sp1++;

	if ((sp2 = memchr(sp1, ' ', pubkey_len - *method_len)) == NULL) {
		/* Assume that the id string is missing, but that it's okay */
		sp2 = pubkey + pubkey_len;
	}

	if (libssh2_base64_decode(session, &tmp, &tmp_len, sp1, sp2 - sp1)) {
		libssh2_error(session, LIBSSH2_ERROR_FILE, "Invalid key data, not base64 encoded", 0);
		LIBSSH2_FREE(session, pubkey);
		return -1;
	}
	*pubkeydata = tmp;
	*pubkeydata_len = tmp_len;

	return 0;
}
/* }}} */

/* {{{ libssh2_file_read_publickey
 * Read a PEM encoded private key from an id_??? style file
 */
static int libssh2_file_read_privatekey(LIBSSH2_SESSION *session,	LIBSSH2_HOSTKEY_METHOD **hostkey_method, void **hostkey_abstract,
																	char *method, int method_len,
																	char *privkeyfile, char *passphrase)
{
	LIBSSH2_HOSTKEY_METHOD **hostkey_methods_avail = libssh2_hostkey_methods();

	*hostkey_method = NULL;
	*hostkey_abstract = NULL;
	while (*hostkey_methods_avail && (*hostkey_methods_avail)->name) {
		if ((*hostkey_methods_avail)->initPEM &&
			strncmp((*hostkey_methods_avail)->name, method, method_len) == 0) {
			*hostkey_method = *hostkey_methods_avail;
			break;
		}
		hostkey_methods_avail++;
	}
	if (!*hostkey_method) {
		libssh2_error(session, LIBSSH2_ERROR_METHOD_NONE, "No handler for specified private key", 0);
		return -1;
	}

	if ((*hostkey_method)->initPEM(session, privkeyfile, passphrase, hostkey_abstract)) {
		libssh2_error(session, LIBSSH2_ERROR_FILE, "Unable to initialize private key from file", 0);
		return -1;
	}

	return 0;
} 
/* }}} */

/* {{{ libssh2_userauth_publickey_fromfile_ex
 * Authenticate using a keypair found in the named files
 */
LIBSSH2_API int libssh2_userauth_publickey_fromfile_ex(LIBSSH2_SESSION *session, char *username, int username_len,
                                                                                 char *publickey, char *privatekey,
                                                                                 char *passphrase)
{
	LIBSSH2_HOSTKEY_METHOD *privkeyobj;
	void *abstract;
	unsigned char buf[5];
	struct iovec datavec[4];
	unsigned char *method, *pubkeydata, *packet, *s, *b, *sig;
	unsigned long method_len, pubkeydata_len, packet_len, sig_len;

	if (libssh2_file_read_publickey(session, &method, &method_len, &pubkeydata, &pubkeydata_len, publickey)) {
		return -1;
	}

	packet_len = username_len + method_len + pubkeydata_len + 45;	/* packet_type(1) + username_len(4) + servicename_len(4) + 
																	   service_name(14)"ssh-connection" + authmethod_len(4) + 
																	   authmethod(9)"publickey" + sig_included(1)'\0' + 
																	   algmethod_len(4) + publickey_len(4) */
	/* Preallocate space for an overall length,  method name again,
	 * and the signature, which won't be any larger than the size of the publickeydata itself */
	s = packet = LIBSSH2_ALLOC(session, packet_len + 4 + (4 + method_len) + (4 + pubkeydata_len));

	*(s++) = SSH_MSG_USERAUTH_REQUEST;
	libssh2_htonu32(s, username_len);				s += 4;
	memcpy(s, username, username_len);				s += username_len;

	libssh2_htonu32(s, 14);							s += 4;
	memcpy(s, "ssh-connection", 14);				s += 14;

	libssh2_htonu32(s, 9);							s += 4;
	memcpy(s, "publickey", 9);						s += 9;

	b = s;
	*(s++) = 0; /* Not sending signature with *this* packet */

	libssh2_htonu32(s, method_len);					s += 4;
	memcpy(s, method, method_len);					s += method_len;

	libssh2_htonu32(s, pubkeydata_len);				s += 4;
	memcpy(s, pubkeydata, pubkeydata_len);			s += pubkeydata_len;

	if (libssh2_packet_write(session, packet, packet_len)) {
		libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND, "Unable to send userauth-publickey request", 0);
		LIBSSH2_FREE(session, packet);
		LIBSSH2_FREE(session, method);
		LIBSSH2_FREE(session, pubkeydata);
		return -1;
	}

	while (1) {
		unsigned char *data;
		unsigned long data_len;

		if (libssh2_packet_ask(session, SSH_MSG_USERAUTH_SUCCESS, &data, &data_len, 1) == 0) {
			/* God help any SSH server that allows an UNVERIFIED public key to validate the user */
			LIBSSH2_FREE(session, data);
			LIBSSH2_FREE(session, packet);
			LIBSSH2_FREE(session, method);
			LIBSSH2_FREE(session, pubkeydata);
			session->authenticated = 1;
			return 0;
		}

		if (libssh2_packet_ask(session, SSH_MSG_USERAUTH_FAILURE, &data, &data_len, 0) == 0) {
			/* This public key is not allowed for this user on this server */
			LIBSSH2_FREE(session, data);
			LIBSSH2_FREE(session, packet);
			LIBSSH2_FREE(session, method);
			LIBSSH2_FREE(session, pubkeydata);
			libssh2_error(session, LIBSSH2_ERROR_PUBLICKEY_UNRECOGNIZED, "Username/PublicKey combination invalid", 0);
			return -1;
		}

		if (libssh2_packet_ask(session, SSH_MSG_USERAUTH_PK_OK, &data, &data_len, 0) == 0) {
			/* Semi-Success! */
			if ((libssh2_ntohu32(data + 1) != method_len) ||
				strncmp(data + 5, method, method_len) ||
				(libssh2_ntohu32(data + 5 + method_len) != pubkeydata_len) ||
				strncmp(data + 5 + method_len + 4, pubkeydata, pubkeydata_len)) {
				/* Unlikely but possible, the server has responded to a different userauth public key request */
				LIBSSH2_FREE(session, data);
				continue;
			}
			LIBSSH2_FREE(session, data);
			break;
		}
		/* TODO: Timeout? */
	}
	LIBSSH2_FREE(session, pubkeydata);

	if (libssh2_file_read_privatekey(session, &privkeyobj, &abstract, method, method_len, privatekey, passphrase)) {
		LIBSSH2_FREE(session, method);
		LIBSSH2_FREE(session, packet);
		return -1;
	}

	*b = 0xFF;

	libssh2_htonu32(buf, session->session_id_len);
	datavec[0].iov_base = buf;
	datavec[0].iov_len = 4;
	datavec[1].iov_base = session->session_id;
	datavec[1].iov_len = session->session_id_len;
	datavec[2].iov_base = packet;
	datavec[2].iov_len = packet_len;

	if (privkeyobj->signv(session, &sig, &sig_len, 3, datavec, &abstract)) {
		LIBSSH2_FREE(session, method);
		LIBSSH2_FREE(session, packet);
		if (privkeyobj->dtor) {
			privkeyobj->dtor(session, &abstract);
		}
		return -1;
	}

	if (privkeyobj->dtor) {
		privkeyobj->dtor(session, &abstract);
	}

	if (sig_len > pubkeydata_len) {
		/* Should *NEVER* happen, but...well.. better safe than sorry */
		packet = LIBSSH2_REALLOC(session, packet, packet_len + 4 + (4 + method_len) + (4 + sig_len)); /* PK sigblob */
		if (!packet) {
			libssh2_error(session, LIBSSH2_ERROR_ALLOC, "Failed allocating additional space for userauth-publickey packet", 0);
			LIBSSH2_FREE(session, method);
			return -1;
		}
	}

	s = packet + packet_len;

	libssh2_htonu32(s, 4 + method_len + 4 + sig_len);	s += 4;

	libssh2_htonu32(s, method_len);						s += 4;
	memcpy(s, method, method_len);						s += method_len;
	LIBSSH2_FREE(session, method);

	libssh2_htonu32(s, sig_len);						s += 4;
	memcpy(s, sig, sig_len);							s += sig_len;
	LIBSSH2_FREE(session, sig);

	if (libssh2_packet_write(session, packet, s - packet)) {
		libssh2_error(session, LIBSSH2_ERROR_SOCKET_SEND, "Unable to send userauth-publickey request", 0);
		LIBSSH2_FREE(session, packet);
		return -1;
	}
	LIBSSH2_FREE(session, packet);

	while (1) {
		unsigned char *data;
		unsigned long data_len;

		if (libssh2_packet_ask(session, SSH_MSG_USERAUTH_SUCCESS, &data, &data_len, 1) == 0) {
			/* We are us and we've proved it. */
			LIBSSH2_FREE(session, data);
			session->authenticated = 1;
			return 0;
		}

		if (libssh2_packet_ask(session, SSH_MSG_USERAUTH_FAILURE, &data, &data_len, 0) == 0) {
			/* This public key is not allowed for this user on this server */
			LIBSSH2_FREE(session, data);
			libssh2_error(session, LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED, "Invalid signature for supplied public key, or bad username/public key combination", 0);
			return -1;
		}
		/* TODO: Timeout? */
	}
	
	return 0;
}
/* }}} */
