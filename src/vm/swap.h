#ifndef VM_SWAP_H
#define VM_SWAP_H

typedef size_t sid_t;

/* Initialize the swap table. */
void swap_init();

/* Move the content of the user virtual page to the swap disk
 * and return the index of the swap region where the content
 * is placed. */
sid_t swap_out(void *upage);


/* Move the content of the 'sid'-th swap region to the user
 * virtual page. */
void swap_in(sid_t sid, void *upage);

/* Free the 'sid'-th swap region. */
void swap_free(sid_t sid);

#endif //VM_SWAP_H
