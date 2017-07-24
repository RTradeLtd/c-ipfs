/***
 * A queue for requests from remote peers
 * NOTE: This should handle multiple threads
 */

#include <stdlib.h>
#include "ipfs/cid/cid.h"
#include "ipfs/exchange/bitswap/peer_request_queue.h"

/**
 * Allocate resources for a new PeerRequest
 * @returns a new PeerRequest struct or NULL if there was a problem
 */
struct PeerRequest* ipfs_bitswap_peer_request_new() {
	struct PeerRequest* request = (struct PeerRequest*) malloc(sizeof(struct PeerRequest));
	if (request != NULL) {
		request->peer_id = 0;
		request->cid = NULL;
	}
	return request;
}

/**
 * Free resources from a PeerRequest
 * @param request the request to free
 * @returns true(1)
 */
int ipfs_bitswap_peer_request_free(struct PeerRequest* request) {
	free(request);
	return 1;
}

/**
 * Allocate resources for a new queue
 */
struct PeerRequestQueue* ipfs_bitswap_peer_request_queue_new() {
	struct PeerRequestQueue* queue = malloc(sizeof(struct PeerRequestQueue));
	if (queue != NULL) {
		pthread_mutex_init(&queue->queue_mutex, NULL);
		queue->first = NULL;
		queue->last = NULL;
	}
	return queue;
}

/**
 * Free all resources related to the queue
 * @param queue the queue
 * @returns true(1)
 */
int ipfs_bitswap_peer_request_queue_free(struct PeerRequestQueue* queue) {
	pthread_mutex_lock(&queue->queue_mutex);
	struct PeerRequestEntry* current = queue->last;
	while (current != NULL) {
		struct PeerRequestEntry* prior = current->prior;
		ipfs_bitswap_peer_request_entry_free(current);
		current = prior;
	}
	pthread_mutex_unlock(&queue->queue_mutex);
	free(queue);
	return 1;
}

/**
 * Adds a peer request to the end of the queue
 * @param queue the queue
 * @param request the request
 * @returns true(1) on success, otherwise false
 */
int ipfs_bitswap_peer_request_queue_add(struct PeerRequestQueue* queue, struct PeerRequest* request) {
	if (request != NULL) {
		struct PeerRequestEntry* entry = ipfs_bitswap_peer_request_entry_new();
		entry->current = request;
		pthread_mutex_lock(&queue->queue_mutex);
		entry->prior = queue->last;
		queue->last = entry;
		if (queue->first == NULL) {
			queue->first = entry;
		}
		pthread_mutex_unlock(&queue->queue_mutex);
		return 1;
	}
	return 0;
}

/**
 * Removes a peer request from the queue, no mather where it is
 * @param queue the queue
 * @param request the request
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_bitswap_peer_request_queue_remove(struct PeerRequestQueue* queue, struct PeerRequest* request) {
	if (request != NULL) {
		struct PeerRequestEntry* entry = ipfs_bitswap_peer_request_queue_find_entry(queue, request);
		if (entry != NULL) {
			pthread_mutex_lock(&queue->queue_mutex);
			// remove the entry's link, and hook prior and next together
			entry->prior->next = entry->next;
			entry->prior = NULL;
			entry->next = NULL;
			ipfs_bitswap_peer_request_entry_free(entry);
			pthread_mutex_unlock(&queue->queue_mutex);
			return 1;
		}
	}
	return 0;
}

/**
 * Finds a PeerRequestEntry that contains the specified PeerRequest
 * @param queue the queue to look through
 * @param request what we're looking for
 * @returns the PeerRequestEntry or NULL if not found
 */
struct PeerRequestEntry* ipfs_bitswap_peer_request_queue_find_entry(struct PeerRequestQueue* queue, struct PeerRequest* request) {
	if (request != NULL) {
		struct PeerRequestEntry* current = queue->first;
		while (current != NULL) {
			if (current->current->peer_id == request->peer_id && ipfs_cid_compare(request->cid, current->current->cid) == 0) {
				return current;
			}
		}
	}
	return NULL;
}


/**
 * Pull a PeerRequest off the queue
 * @param queue the queue
 * @returns the PeerRequest that should be handled next, or NULL if the queue is empty
 */
struct PeerRequest* ipfs_bitswap_peer_request_pop(struct PeerRequestQueue* queue) {
	struct PeerRequest* retVal = NULL;
	if (queue != NULL) {
		pthread_mutex_lock(&queue->queue_mutex);
		struct PeerRequestEntry* entry = queue->first;
		retVal = entry->current;
		queue->first = queue->first->next;
		pthread_mutex_unlock(&queue->queue_mutex);
		ipfs_bitswap_peer_request_entry_free(entry);
	}
	return retVal;
}

/***
 * Allocate resources for a PeerRequestEntry struct
 * @returns the allocated struct or NULL if there was a problem
 */
struct PeerRequestEntry* ipfs_bitswap_peer_request_entry_new() {
	struct PeerRequestEntry* entry = (struct PeerRequestEntry*) malloc(sizeof(struct PeerRequestEntry));
	if (entry != NULL) {
		entry->current = NULL;
		entry->next = NULL;
		entry->prior = NULL;
	}
	return entry;
}

/**
 * Frees resources allocated
 * @param entry the PeerRequestEntry to free
 * @returns true(1)
 */
int ipfs_bitswap_peer_request_entry_free(struct PeerRequestEntry* entry) {
	entry->next = NULL;
	entry->prior = NULL;
	ipfs_bitswap_peer_request_free(entry->current);
	entry->current = NULL;
	free(entry);
	return 1;
}

