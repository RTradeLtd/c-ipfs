/**
 * The journal protocol attempts to keep a journal in sync with other (approved) nodes
 */
#include "libp2p/os/utils.h"
#include "ipfs/journal/journal.h"
#include "ipfs/journal/journal_message.h"
#include "ipfs/journal/journal_entry.h"
#include "ipfs/repo/fsrepo/journalstore.h"
#include "ipfs/repo/config/replication.h"

/***
 * See if we can handle this message
 * @param incoming the incoming message
 * @param incoming_size the size of the incoming message
 * @returns true(1) if the protocol in incoming is something we can handle. False(0) otherwise.
 */
int ipfs_journal_can_handle(const uint8_t* incoming, size_t incoming_size) {
	if (incoming_size < 8)
		return 0;
	char* result = strstr((char*)incoming, "/ipfs/journal/1.0.0");
	if(result == NULL || result != (char*)incoming)
		return 0;
	return 1;
}

/**
 * Clean up resources used by this handler
 * @param context the context to clean up
 * @returns true(1)
 */
int ipfs_journal_shutdown_handler(void* context) {
	return 1;
}

/***
 * Handles a message
 * @param incoming the message
 * @param incoming_size the size of the message
 * @param session_context details of the remote peer
 * @param protocol_context in this case, an IpfsNode
 * @returns 0 if the caller should not continue looping, <0 on error, >0 on success
 */
int ipfs_journal_handle_message(const uint8_t* incoming, size_t incoming_size, struct SessionContext* session_context, void* protocol_context) {
	//struct IpfsNode* local_node = (struct IpfsNode*)protocol_context;
	//TODO: handle the message
	return -1;
}

/***
 * Build the protocol handler struct for the Journal protocol
 * @param local_node what to stuff in the context
 * @returns the protocol handler
 */
struct Libp2pProtocolHandler* ipfs_journal_build_protocol_handler(const struct IpfsNode* local_node) {
	struct Libp2pProtocolHandler* handler = (struct Libp2pProtocolHandler*) malloc(sizeof(struct Libp2pProtocolHandler));
	if (handler != NULL) {
		handler->context = (void*)local_node;
		handler->CanHandle = ipfs_journal_can_handle;
		handler->HandleMessage = ipfs_journal_handle_message;
		handler->Shutdown = ipfs_journal_shutdown_handler;
	}
	return handler;
}

/***
 * Retrieve the last n records from the journalstore
 * @param database the reference to the opened db
 * @param n the number of records to retrieve
 * @returns a vector of struct JournalRecord
 */
struct Libp2pVector* ipfs_journal_get_last(struct Datastore* database, int n) {
	struct Libp2pVector* vector = libp2p_utils_vector_new(1);
	if (vector != NULL) {
		void* cursor;
		if (!repo_journalstore_cursor_open(database, &cursor))
			return NULL;
		struct JournalRecord* rec = NULL;
		if (!repo_journalstore_cursor_get(database, cursor, CURSOR_LAST, &rec)) {
			libp2p_utils_vector_free(vector);
			repo_journalstore_cursor_close(database, cursor);
			return NULL;
		}
		// we've got one, now start the loop
		int i = 0;
		do {
			libp2p_utils_vector_add(vector, rec);
			if (!repo_journalstore_cursor_get(database, cursor, CURSOR_PREVIOUS, &rec)) {
				break;
			}
			i++;
		} while(i < n);
		repo_journalstore_cursor_close(database, cursor);
	}
	return vector;
}

int ipfs_journal_free_records(struct Libp2pVector* records) {
	if (records != NULL) {
		for (int i = 0; i < records->total; i++) {
			struct JournalRecord* rec = (struct JournalRecord*)libp2p_utils_vector_get(records, i);
			journal_record_free(rec);
		}
		libp2p_utils_vector_free(records);
	}
	return 1;
}

int ipfs_journal_send_message(struct IpfsNode* node, struct Libp2pPeer* peer, struct JournalMessage* message) {
	if (peer->connection_type != CONNECTION_TYPE_CONNECTED)
		libp2p_peer_connect(&node->identity->private_key, peer, node->peerstore, 10);
	if (peer->connection_type != CONNECTION_TYPE_CONNECTED)
		return 0;
	// protobuf the message
	size_t msg_size = ipfs_journal_message_encode_size(message);
	uint8_t msg[msg_size];
	if (!ipfs_journal_message_encode(message, &msg[0], msg_size, &msg_size))
		return 0;
	// send the header
	char* header = "/ipfs/journalio/1.0.0/n";
	if (!peer->sessionContext->default_stream->write(peer->sessionContext->default_stream, (unsigned char*)header, strlen(header)))
		return 0;
	// send the message
	return peer->sessionContext->default_stream->write(peer->sessionContext->default_stream, msg, msg_size);
}

/***
 * Send a journal message to a remote peer
 * @param replication_peer the peer to send it to
 * @returns true(1) on success, false(0) otherwise.
 */
int ipfs_journal_sync(struct IpfsNode* local_node, struct ReplicationPeer* replication_peer) {
	// make sure we're connected securely
	if (replication_peer->peer->is_local)
		return 0;
	if (replication_peer->peer->sessionContext->secure_stream == NULL)
		return 0;

	// grab the last 10? files
	struct Libp2pVector* journal_records = ipfs_journal_get_last(local_node->repo->config->datastore, 10);
	if (journal_records == NULL || journal_records->total == 0) {
		// nothing to do
		return 1;
	}
	// build the message
	struct JournalMessage* message = ipfs_journal_message_new();
	for(int i = 0; i < journal_records->total; i++) {
		struct JournalRecord* rec = (struct JournalRecord*) libp2p_utils_vector_get(journal_records, i);
		if (rec->timestamp > message->end_epoch)
			message->end_epoch = rec->timestamp;
		if (message->start_epoch == 0 || rec->timestamp < message->start_epoch)
			message->start_epoch = rec->timestamp;
		struct JournalEntry* entry = ipfs_journal_entry_new();
		entry->timestamp = rec->timestamp;
		entry->pin = 1;
		entry->hash_size = rec->hash_size;
		entry->hash = (uint8_t*) malloc(entry->hash_size);
		if (entry->hash == NULL) {
			// out of memory
			ipfs_journal_message_free(message);
			ipfs_journal_free_records(journal_records);
			return 0;
		}
		memcpy(entry->hash, rec->hash, entry->hash_size);
		libp2p_utils_vector_add(message->journal_entries, entry);
	}
	// send the message
	message->current_epoch = os_utils_gmtime();
	int retVal = ipfs_journal_send_message(local_node, replication_peer->peer, message);
	if (retVal) {
		replication_peer->lastConnect = message->current_epoch;
		replication_peer->lastJournalTime = message->end_epoch;
	}
	// clean up
	ipfs_journal_message_free(message);
	ipfs_journal_free_records(journal_records);

	return retVal;
}

