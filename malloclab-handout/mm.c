/*
 * mm-implicit.c -  Simple allocator based on implicit free lists,
 *                  first fit placement, and boundary tag coalescing.
 *
 * Each block has header and footer of the form:
 *
 *      31                     3  2  1  0
 *      -----------------------------------
 *     | s  s  s  s  ... s  s  s  0  0  a/f
 *      -----------------------------------
 *
 * where s are the meaningful size bits and a/f is set
 * if the block is allocated. The list has the following form:
 *
 * begin                                                          end
 * heap                                                           heap
 *  -----------------------------------------------------------------
 * |  pad   | hdr(8:a) | ftr(8:a) | zero or more usr blks | hdr(8:a) |
 *  -----------------------------------------------------------------
 *          |       prologue      |                       | epilogue |
 *          |         block       |                       | block    |
 *
 * The allocated prologue and epilogue blocks are overhead that
 * eliminate edge conditions during coalescing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
  /* Team name */
  "NB",
  /* First member's full name */
  "Huaiqian Yan",
  /* First member's email address */
  "Huaiqian.Yan@colorado.edu",
  /* Second member's full name (leave blank if none) */
  "Jiahao Wang",
  /* Second member's email address (leave blank if none) */
  "jiwa8909@colorado.edu"
};

/////////////////////////////////////////////////////////////////////////////
// Constants and macros
//
// These correspond to the material in Figure 9.43 of the text
// The macros have been turned into C++ inline functions to
// make debugging code easier.
//
/////////////////////////////////////////////////////////////////////////////
#define WSIZE       4       /* word size (bytes) */
#define DSIZE       8       /* doubleword size (bytes) */
#define CHUNKSIZE  ((1<<13)+(1<<12)-(1<<11))  /* initial heap size (bytes), its the best size in this implement*/
#define OVERHEAD    8       /* overhead of header and footer (bytes) */

//suitable conditions for void* bp, called in free() and realloc()
#define VALID_BP(bp) (mem_heap_lo() <= bp && mem_heap_hi() >= bp && ((GET(FTRP(bp)) & 0x7) == 1) && (((uintptr_t)(bp)) % 8 == 0))

/*
static inline int MAX(int x, int y) {
  return x > y ? x : y;
}*/

//
// Pack a size and allocated bit into a word
// We mask of the "alloc" field to insure only
// the lower bit is used
//
static inline uint32_t PACK(uint32_t size, int alloc) {
  return ((size) | (alloc & 0x1));
}

//
// Read and write a word at address p
//
static inline uint32_t GET(void *p) { return  *(uint32_t *)p; }
static inline void PUT( void *p, uint32_t val)
{
  *((uint32_t *)p) = val;
}

//
// Read the size and allocated fields from address p
//
static inline uint32_t GET_SIZE( void *p )  {
  return GET(p) & ~0x7;
}

static inline int GET_ALLOC( void *p  ) {
  return GET(p) & 0x1;
}

//
// Given block ptr bp, compute address of its header and footer
//
static inline void *HDRP(void *bp) {
  return ( (char *)bp) - WSIZE;
}
static inline void *FTRP(void *bp) {
  return ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE);
}

//
// Given block ptr bp, compute address of next and previous blocks
//
static inline void *NEXT_BLKP(void *bp) {
  return  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)));
}

static inline void* PREV_BLKP(void *bp){
  return  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)));
}

/////////////////////////////////////////////////////////////////////////////
//
// Global Variables
//

static char *heap_listp;  /* pointer to first block */

//
// function prototypes for internal helper routines
//
static void *extend_heap(uint32_t words);
static void place(void *bp, uint32_t asize);
static void *find_fit(uint32_t asize);
static void *coalesce(void *bp);
static void printblock(void *bp);
static void checkblock(void *bp);
//
// mm_init - Initialize the memory manager
//
int mm_init(void)
{
  // create the initial empty heap
  if((heap_listp = mem_sbrk(CHUNKSIZE)) == (void*)-1){ //mem_sbrk(CHUNKSIZE) expand heap by chunksize returns
  													   //a generic pointer to the first byte of the newly allocated
  													   //heap area. If it is -1, there is a problem with initialization
  	return -1;

  } else{
  	// initialize the prologue block where the heap starts
  	PUT(heap_listp + 4, PACK(DSIZE, 1));//set header, according to the comments
  	PUT(heap_listp + DSIZE, PACK(DSIZE, 1));//set footer, according to the comments

    // initialize the first free block
  	PUT(heap_listp + 12, PACK(CHUNKSIZE-4 - 12, 0));//Initialize header, pad + prologue block = 12, epilogue block is 4
  	PUT(heap_listp + CHUNKSIZE - DSIZE, PACK(CHUNKSIZE-4 - 12, 0));//Initialize footer

    // initialize the epilogue header where the heap ends
  	PUT(heap_listp + CHUNKSIZE - WSIZE, PACK(DSIZE, 1));//set header, according to the comments

    // locate heap_listp in the first free block
  	heap_listp += 16;

  	return 0;
  }
}

//
// extend_heap - Extend heap with free block and return its block pointer
//
static void *extend_heap(uint32_t words)
{
  // Different from book, this function allocate the words requested by mm_malloc()

  // store the last address of the old heap
  char * t = mem_heap_hi(); //mem_heap_hi: Returns a generic pointer to the last byte in the heap.

  // extend heap
  if(mem_sbrk(words) == (void*)-1)
  	return NULL;

  //initialize the block header/footer and the epilogue block
  PUT(t + 1 - WSIZE, PACK(words, 1));//block header, the block will be used exactly. Therefore, set the bit to 1.
  PUT(t + 1 + words - DSIZE, PACK(words, 1));//block footer, same as above
  PUT(t + 1 + words - WSIZE, PACK(DSIZE, 1));//epilogue block
  return t + 1;
}

//
// Practice problem 9.8
//
// find_fit - Find a fit for a block with asize bytes
//
static void *find_fit(uint32_t asize)// first fit method
{
  char * t = heap_listp;

  // loop ends when block is free and large enough
  while(GET_ALLOC(HDRP(t)) || (asize > GET_SIZE(HDRP(t))) ) { //Loop when the block is allocated
  	                                                          //or asize is bigger than the block size
  	t = NEXT_BLKP(t);

    // if pointer goes to the end of heap
  	if(t == (char*)mem_heap_hi()+1)
  		return NULL; /* no fit */

  }
  return t;
}

//
//
// Practice problem 9.9
//
// place - Place block of asize bytes at start of free block bp
//         and split if remainder would be at least minimum block size
//
static void place(void *bp, uint32_t asize)
{
  /*different from book, the smallest block is 2 words.
    it is useless, but may be coalesced in coalesce(). */

    //Case 1: the free block is larger. We need to do split the left free block out
	if(GET_SIZE(HDRP(bp)) >= asize + DSIZE){
		//free the remaining block.
		PUT(FTRP(bp), PACK(GET_SIZE(HDRP(bp)) - asize, 0));//The original footer will still be the free block footer
		PUT(HDRP(bp) + asize, PACK(GET_SIZE(HDRP(bp)) - asize, 0));//GET_SIZE(HDRP(bp)) - asize = the remaining free block
																   //Header of the free block
		//Mark as allocated
		PUT(HDRP(bp), PACK(asize, 1));//Header of allocated block
		PUT(HDRP(bp) + asize - WSIZE, PACK(asize, 1));//footer of allocated block
	}

	//Case 2: The free block is just equal to what we need
	else{
		//Mark as allocated
		PUT(HDRP(bp), PACK(GET_SIZE(HDRP(bp)), 1));
		PUT(FTRP(bp), PACK(GET_SIZE(HDRP(bp)), 1));
	}
}

//
// mm_free - Free a block
//
void mm_free(void *bp)
{
  //if bp is the appropriate pointer
  if(VALID_BP(bp)){
    //free the block
  	PUT(HDRP(bp), GET(HDRP(bp))-1);//The lowest bit of the allocated block is 1.
  	                               //Therefore, "GET(HDRP(bp))-1" is equivalent to PACK(size, 0)
  	PUT(FTRP(bp), GET(FTRP(bp))-1);
    //if its previous or the next block is free, coalesce them
  	coalesce(bp);
  }
}

//
// coalesce - boundary tag coalescing. Return ptr to coalesced block
//
static void *coalesce(void *bp)
{
  //Different from book,
  //if prev blk is free point the pointer to the previous one preparing for the next test

  if(!GET_ALLOC(HDRP(PREV_BLKP(bp)))){
  	bp = PREV_BLKP(bp);//goto the prev block
  	PUT(FTRP(NEXT_BLKP(bp)), GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(NEXT_BLKP(bp))));//prev size plus itself
  	PUT(HDRP(bp), GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(NEXT_BLKP(bp))));
  }
  //if next blk is free
  if(!GET_ALLOC(HDRP(NEXT_BLKP(bp)))){
  	// cannot change the following order, for FTRP() and NEXT_BLKP() depends on HDRP()
  	PUT(FTRP(NEXT_BLKP(bp)), GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(NEXT_BLKP(bp))));
  	PUT(HDRP(bp), GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(NEXT_BLKP(bp))));
  }
  return bp;
}

//
// mm_malloc - Allocate a block with at least size bytes of payload
//
void *mm_malloc(uint32_t size)
{
    // find appropriate block

    //make 'size' a multiple of 8
    while(size % 8 != 0)//alignment
    	++size;

    //find if there is a free block to fit 'size' bytes
    char* t = find_fit(size + DSIZE);
    if(t) {
        place(t, size + DSIZE);

    } else {
        //if there exists no appropriate block，extend heap.
        t = extend_heap(size + DSIZE);
        if(!t)
            return NULL;
    }
    return t;
}

//
// mm_realloc -- returns a pointer to an allocated region of at least size bytes
void *mm_realloc(void *ptr, uint32_t size)
{
  void *newp;

  // if ptr is NULL, this function acts as mm_malloc()
  if(!ptr)
  	return mm_malloc(size);

  if(VALID_BP(ptr)){
    //make 'size' a multiple of 8
  	while(size % 8 != 0)
    	++size;

    /* If the size of the program request is equal to the size of the block,
      which is pointed to by the ptr, this function will do nothing. */
    if(GET_SIZE(HDRP(ptr)) == size + DSIZE){
    	return ptr;
    }

    /*if the original block size is larger than the requested,
      try to split it into two blocks and return the original ptr. */
    if(GET_SIZE(HDRP(ptr)) > size + DSIZE){//size + DSIZE = hdr + usr blocks + ftr

      //split if the remaining block is larger than the smallest block(8 Bytes)
    	if(GET_SIZE(HDRP(ptr)) >= size + DSIZE + DSIZE){
  	    //free the remaining block.
  			PUT(FTRP(ptr), PACK(GET_SIZE(HDRP(ptr)) - size - DSIZE, 0));//reset footer size
  			PUT(HDRP(ptr) + size + DSIZE, PACK(GET_SIZE(HDRP(ptr)) - size - DSIZE, 0));
  			// write the new size on the header and footer
  			PUT(HDRP(ptr), PACK(size + DSIZE, 1));
  			PUT(HDRP(ptr) + size + WSIZE, PACK(size + DSIZE, 1));
  			return ptr;
		  }

    /* in this case, the original block size is smaller than the requested,
      so we must try to coalesce the next free block to extend the block size
      or just use mm_malloc() to allocate a new block */
    } else{
    	// try coalesce
    	if(!GET_ALLOC(HDRP(NEXT_BLKP(ptr))) && (GET_SIZE(HDRP(ptr)) + GET_SIZE(HDRP(NEXT_BLKP(ptr))) >= size + DSIZE) ){
    		// coalesce
  			PUT(FTRP(NEXT_BLKP(ptr)), PACK(GET_SIZE(HDRP(ptr)) + GET_SIZE(HDRP(NEXT_BLKP(ptr))), 1) );
  			PUT(HDRP(ptr), PACK(GET_SIZE(HDRP(ptr)) + GET_SIZE(HDRP(NEXT_BLKP(ptr))), 1) );
        /* Recursive call. After coalesce, test the above conditions again.*/
  			return mm_realloc(ptr, size);

      /* in this case, coalesced failed. call mm_malloc() and copy the original content to
        the new block */
    	} else{
    		newp = mm_malloc(size);
    		if(newp){
    			memcpy(newp, ptr, GET_SIZE(HDRP(ptr)) - DSIZE);
          //free the original block
    			mm_free(ptr);
    			return newp;
    		}
    	}
    }
  }
  return NULL;
}

//
// mm_checkheap - Check the heap for consistency
//
void mm_checkheap(int verbose)
{
  //
  // This provided implementation assumes you're using the structure
  // of the sample solution in the text. If not, omit this code
  // and provide your own mm_checkheap
  //
  void *bp = heap_listp;

  if (verbose) {
    printf("Heap (%p):\n", heap_listp);
  }

  if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || !GET_ALLOC(HDRP(heap_listp))) {
	printf("Bad prologue header\n");
  }
  checkblock(heap_listp);

  for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    if (verbose)  {
      printblock(bp);
    }
    checkblock(bp);
  }

  if (verbose) {
    printblock(bp);
  }

  if ((GET_SIZE(HDRP(bp)) != 0) || !(GET_ALLOC(HDRP(bp)))) {
    printf("Bad epilogue header\n");
  }
}

static void printblock(void *bp)
{
  uint32_t hsize, halloc, fsize, falloc;

  hsize = GET_SIZE(HDRP(bp));
  halloc = GET_ALLOC(HDRP(bp));
  fsize = GET_SIZE(FTRP(bp));
  falloc = GET_ALLOC(FTRP(bp));

  if (hsize == 0) {
    printf("%p: EOL\n", bp);
    return;
  }

  printf("%p: header: [%d:%c] footer: [%d:%c]\n",
	 bp,
	 (int) hsize, (halloc ? 'a' : 'f'),
	 (int) fsize, (falloc ? 'a' : 'f'));
}

static void checkblock(void *bp)
{
  if ((uintptr_t)bp % 8) {
    printf("Error: %p is not doubleword aligned\n", bp);
  }
  if (GET(HDRP(bp)) != GET(FTRP(bp))) {
    printf("Error: header does not match footer\n");
  }
}

