/* Z_zone.c */

/*
==============================================================================

						ZONE MEMORY ALLOCATION

There is never any space between memblocks, and there will never be two
contiguous free memblocks.

The rover can be left pointing at a non-empty block

It is of no value to free a cachable block, because it will get overwritten
automatically if needed

==============================================================================
*/

#include "doomdef.h"

#if RANGECHECK
#define DEBUG_ 1
#else
#define DEBUG_ 0
#endif

extern uint32_t NextFrameIdx;
memzone_t *mainzone;

#if !DEBUG_

/*
========================
=
= Z_Init
=
========================
*/

void Z_Init(void)
{
	uint8_t *mem = (uint8_t *)memalign(32, MEM_HEAP_SIZE);

	if (!mem)
		I_Error("failed to allocate %08x zone heap");

	/* mars doesn't have a refzone */
	mainzone = Z_InitZone(mem, MEM_HEAP_SIZE);
}

/*
========================
=
= Z_InitZone
=
========================
*/

memzone_t *Z_InitZone(uint8_t *base, int size)
{
	memzone_t *zone;

	memset(base, 0, MEM_HEAP_SIZE);

	zone = (memzone_t *)base;
	zone->size = size;
	zone->rover = &zone->blocklist;
	zone->rover2 = &zone->blocklist;
	zone->rover3 = &zone->blocklist;
	zone->blocklist.size = size - (int)((uint8_t *)&zone->blocklist - (uint8_t *)zone);
	zone->blocklist.user = NULL;
	zone->blocklist.tag = 0;
	zone->blocklist.id = ZONEID;
	zone->blocklist.next = NULL;
	zone->blocklist.prev = NULL;

	return zone;
}

/*
========================
=
= Z_SetAllocBase
= Exclusive Doom64
=
========================
*/

void Z_SetAllocBase(memzone_t *mainzone)
{
	mainzone->rover2 = mainzone->rover;
}

/*
========================
=
= Z_Malloc2
=
= You can pass a NULL user if the tag is < PU_PURGELEVEL
========================
*/

void *Z_Malloc2(memzone_t *mainzone, int size, int tag, void *user)
{
	int extra;
	memblock_t *start, *rover, *newblock, *base;

	if (backres[10] != 0xc3)
		I_Error("failed allocation on %i", size);

	/* */
	/* scan through the block list looking for the first free block */
	/* of sufficient size, throwing out any purgable blocks along the way */
	/* */

	size += sizeof(memblock_t); /* account for size of block header */
	size = BLOCKALIGN(size, 32); /* phrase align everything */

	start = base = mainzone->rover;

	while (base->user || base->size < size) {
		if (base->user)
			rover = base;
		else
			rover = base->next;

		if (!rover)
			goto backtostart;

		if (rover->user) {
			if (!(rover->tag & PU_PURGELEVEL)) {
				if ((!(rover->tag & PU_CACHE)) || ((uint32_t)rover->lockframe >= (NextFrameIdx - 1))) {
					/* hit an in use block, so move base past it */
					base = rover->next;
					if (!base) {
backtostart:
						base = mainzone->rover2;
					}

					if (base == start) /* scaned all the way around the list */
						I_Error("failed allocation on %i", size);

					continue;
				}
			}

			/* */
			/* free the rover block (adding the size to base) */
			/* */
			Z_Free((uint8_t *)rover + sizeof(memblock_t)); /* mark as free */
		}

		if (base != rover) { /* merge with base */
			base->size += rover->size;
			base->next = rover->next;
			if (rover->next)
				rover->next->prev = base;
			else
				mainzone->rover3 = base;
		}
	}

	/* */
	/* found a block big enough */
	/* */
	extra = base->size - size;
	if (extra > MINFRAGMENT) {
		/* there will be a free fragment after the allocated block */
		newblock = (memblock_t *)((uint8_t *)base + size);
		newblock->prev = base;
		newblock->next = base->next;
		if (newblock->next)
			newblock->next->prev = newblock;
		else
			mainzone->rover3 = newblock;

		base->next = newblock;
		base->size = size;

		newblock->size = extra;
		newblock->user = NULL; /* free block */
		newblock->tag = 0;
		newblock->id = ZONEID;
	}

	if (user) {
		base->user = user; /* mark as an in use block */
		*(void **)user = (void *)((uint8_t *)base + sizeof(memblock_t));
	} else {
		if (tag >= PU_PURGELEVEL)
			I_Error("an owner is required for purgable blocks");
		base->user = (void *)1; /* mark as in use, but unowned	 */
	}

	base->tag = tag;
	base->id = ZONEID;
	base->lockframe = NextFrameIdx;

	mainzone->rover = base->next; /* next allocation will start looking here */
	if (!mainzone->rover) {
		mainzone->rover3 = base;
		mainzone->rover = mainzone->rover2;
	}

	return (void *)((uint8_t *)base + sizeof(memblock_t));
}

/*
========================
=
= Z_Alloc2
=
= You can pass a NULL user if the tag is < PU_PURGELEVEL
= Exclusive Psx Doom
========================
*/

void *Z_Alloc2(memzone_t *mainzone, int size, int tag, void *user)
{
	int extra;
	memblock_t *rover, *base, *block, *newblock;

	/* */
	/* scan through the block list looking for the first free block */
	/* of sufficient size, throwing out any purgable blocks along the way */
	/* */

	size += sizeof(memblock_t); /* account for size of block header */
	size = BLOCKALIGN(size, 32); /* phrase align everything */

	base = mainzone->rover3;

	while (base->user || base->size < size) {
		if (base->user) {
			rover = base;
		} else {
			/* hit an in use block, so move base past it */
			rover = base->prev;
			if (!rover)
				I_Error("failed allocation on %i", size);
		}

		if (rover->user) {
			if (!(rover->tag & PU_PURGELEVEL)) {
				if ((!(rover->tag & PU_CACHE)) || ((uint32_t)rover->lockframe >= (NextFrameIdx - 1))) {
					/* hit an in use block, so move base past it */
					base = rover->prev;
					if (!base)
						I_Error("failed allocation on %i", size);

					continue;
				}
			}

			/* */
			/* free the rover block (adding the size to base) */
			/* */
			Z_Free((uint8_t *)rover + sizeof(memblock_t)); /* mark as free */
		}

		if (base != rover) {
			/* merge with base */
			rover->size += base->size;
			rover->next = base->next;

			if (base->next)
				base->next->prev = rover;
			else
				mainzone->rover3 = rover;

			base = rover;
		}
	}

	/* */
	/* found a block big enough */
	/* */
	extra = (base->size - size);

	newblock = base;
	block = base;

	if (extra > MINFRAGMENT) {
		block = (memblock_t *)((uint8_t *)base + extra);
		block->prev = newblock;
		block->next = (void *)newblock->next;

		if (newblock->next)
			newblock->next->prev = block;

		newblock->next = block;
		block->size = size;
		newblock->size = extra;
		newblock->user = 0;
		newblock->tag = 0;
		newblock->id = ZONEID;
	}

	if (block->next == 0)
		mainzone->rover3 = block;

	if (user) {
		block->user = user; /* mark as an in use block */
		*(void **)user = (void *)((uint8_t *)block + sizeof(memblock_t));
	} else {
		if (tag >= PU_PURGELEVEL)
			I_Error("an owner is required for purgable blocks");
		block->user = (void *)1; /* mark as in use, but unowned	 */
	}

	block->id = ZONEID;
	block->tag = tag;
	block->lockframe = NextFrameIdx;

	return (void *)((uint8_t *)block + sizeof(memblock_t));
}

/*
========================
=
= Z_Free2
=
========================
*/

void Z_Free2(memzone_t *mainzone, void *ptr)
{
	(void)mainzone;
	memblock_t *block;

	block = (memblock_t *)((uint8_t *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID)
		I_Error("freed a pointer without ZONEID");

	if (block->user > (void **)0x100) /* smaller values are not pointers */
		*block->user = 0; /* clear the user's mark */
	block->user = NULL; /* mark as free */
	block->tag = 0;
}

/*
========================
=
= Z_FreeTags
=
========================
*/

void Z_FreeTags(memzone_t *mainzone, int tag)
{
	memblock_t *block, *next;

	for (block = &mainzone->blocklist; block; block = next) {
		/* get link before freeing */
		next = block->next;

		/* free block */
		if (block->user) {
			if (block->tag & tag)
				Z_Free((uint8_t *)block + sizeof(memblock_t));
		}
	}

	for (block = &mainzone->blocklist; block; block = next) {
		/* get link before freeing */
		next = block->next;

		if (!block->user && next && !next->user) {
			block->size += next->size;
			block->next = next->next;
			if (next->next)
				next->next->prev = block;
			next = block;
		}
	}

	mainzone->rover = &mainzone->blocklist;
	mainzone->rover2 = &mainzone->blocklist;
	mainzone->rover3 = &mainzone->blocklist;

	block = mainzone->blocklist.next;
	while (block) {
		mainzone->rover3 = block;
		block = block->next;
	}
}

/*
========================
=
= Z_Touch
= Exclusive Doom64
=
========================
*/

void Z_Touch(void *ptr)
{
	memblock_t *block;
	block = (memblock_t *)((uint8_t *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID)
		I_Error("touched a pointer without ZONEID");

	block->lockframe = NextFrameIdx;
}

/*
========================
=
= Z_CheckZone
=
========================
*/

void Z_CheckZone(memzone_t *mainzone)
{
	memblock_t *checkblock;
	int size;

	for (checkblock = &mainzone->blocklist; checkblock;
		 checkblock = checkblock->next) {
		if (checkblock->id != ZONEID)
			I_Error("block missing ZONEID");

		if (!checkblock->next) {
			size = (uint8_t *)checkblock + checkblock->size - (uint8_t *)mainzone;
			if (size != mainzone->size)
				I_Error("zone size changed from %d to %d", mainzone->size, size);
			break;
		}

		if ((uint8_t *)checkblock + checkblock->size != (uint8_t *)checkblock->next)
			I_Error("block size does not touch the next block");
		if (checkblock->next->prev != checkblock)
			I_Error("next block doesn't have proper back link");
	}
}

/*
========================
=
= Z_ChangeTag
=
========================
*/

void Z_ChangeTag(void *ptr, int tag)
{
	memblock_t *block;

	block = (memblock_t *)((uint8_t *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID)
		I_Error("freed a pointer without ZONEID");
	if (tag >= PU_PURGELEVEL && (int)block->user < 0x100)
		I_Error("an owner is required for purgable blocks");
	block->tag = tag;
	block->lockframe = NextFrameIdx;
}

/*
========================
=
= Z_FreeMemory
=
========================
*/

int Z_FreeMemory(memzone_t *mainzone)
{
	memblock_t *block;
	int free;

	free = 0;
	for (block = &mainzone->blocklist; block; block = block->next) {
		if (!block->user)
			free += block->size;
	}

	return free;
}

/*
========================
=
= Z_DumpHeap
=
========================
*/

void Z_DumpHeap(memzone_t *mainzone)
{
	return;
}

/*
========================
=
= Z_Defragment
= Merges adjacent free blocks in the memory zone
=
========================
*/

void Z_Defragment(memzone_t *zone)
{
	memblock_t *block, *next;

	// Start with the first block
	block = &zone->blocklist;

	while (block) {
		next = block->next;

		// If the current block and the next block are both free
		if (!block->user && next && !next->user) {
			// Merge the blocks
			block->size += next->size;
			block->next = next->next;

			if (next->next) {
				next->next->prev = block;
			} else {
				// Update rover3 if we've merged with the last block
				zone->rover3 = block;
			}

			// Do not advance to the next block to handle chained merges
			continue;
		}

		// Move to the next block
		block = next;
	}

	// Reset rovers to avoid any dangling pointers
	zone->rover = &zone->blocklist;
	zone->rover2 = &zone->blocklist;
	zone->rover3 = &zone->blocklist;

	// Update rover3 to the last block in the list
	block = &zone->blocklist;
	while (block->next) {
		block = block->next;
		zone->rover3 = block;
	}
}

#else

/*
========================
=
= Z_Init
=
========================
*/

void Z_Init(void)
{
	uint8_t *mem = (uint8_t *)memalign(32,MEM_HEAP_SIZE);

	if (!mem)
		I_Error("failed to allocate %08x zone heap", MEM_HEAP_SIZE);

	/* mars doesn't have a refzone */
	mainzone = Z_InitZone(mem, MEM_HEAP_SIZE);
}

/*
========================
=
= Z_InitZone
=
========================
*/

memzone_t *Z_InitZone(uint8_t *base, int size)
{
	memzone_t *zone;

	zone = (memzone_t *)base;
	zone->size = size;
	zone->rover = &zone->blocklist;
	zone->rover2 = &zone->blocklist;
	zone->rover3 = &zone->blocklist;
	zone->blocklist.size =
		size - (int)((uint8_t *)&zone->blocklist - (uint8_t *)zone);
	zone->blocklist.user = NULL;
	zone->blocklist.tag = 0;
	zone->blocklist.id = ZONEID;
	zone->blocklist.next = NULL;
	zone->blocklist.prev = NULL;

	return zone;
}
/*
========================
=
= Z_SetAllocBase
= Exclusive Doom64
=
========================
*/
void Z_SetAllocBase(memzone_t *mainzone)
{
	mainzone->rover2 = mainzone->rover;
}

/*
========================
=
= Z_Malloc2
=
= You can pass a NULL user if the tag is < PU_PURGELEVEL
========================
*/

void *__Z_Malloc2(memzone_t *mainzone, int size, int tag, void *user, uintptr_t retaddr, const char *file, int line)
{
	int extra;
	memblock_t *start, *rover, *newblock, *base;

	dbgio_printf("%s %s:%d\n", __func__, file, line);

	Z_CheckZone(mainzone);

	if (backres[10] != 0xc3) {
		I_Error("failed allocation on %i", size);
	}

	/* */
	/* scan through the block list looking for the first free block */
	/* of sufficient size, throwing out any purgable blocks along the way */
	/* */

	size += sizeof(memblock_t); /* account for size of block header */
	size = BLOCKALIGN(size,32); /* phrase align everything */

	start = base = mainzone->rover;

	while (base->user || base->size < size) {
		if (base->user)
			rover = base;
		else
			rover = base->next;

		if (!rover)
			goto backtostart;

		if (rover->user) {
			if (!(rover->tag & PU_PURGELEVEL)) {
				if (!(rover->tag & PU_CACHE) ||
					(uint32_t)rover->lockframe >= (NextFrameIdx - 1)) {
					/* hit an in use block, so move base past it */
					base = rover->next;
					if (!base) {
backtostart:
						base = mainzone->rover2;
					}

					if (base == start) /* scaned all the way around the list */
					{
						Z_DumpHeap(mainzone);
						I_Error("failed allocation on %i", size);
					}
					continue;
				}
			}

			/* */
			/* free the rover block (adding the size to base) */
			/* */
			Z_Free((uint8_t *)rover + sizeof(memblock_t)); /* mark as free */
		}

		if (base != rover) { /* merge with base */
			base->size += rover->size;
			base->next = rover->next;
			if (rover->next)
				rover->next->prev = base;
			else
				mainzone->rover3 = base;
		}
	}

	/* */
	/* found a block big enough */
	/* */
	extra = base->size - size;
	if (extra > MINFRAGMENT) {
		/* there will be a free fragment after the allocated block */
		newblock = (memblock_t *)((uint8_t *)base + size);
		newblock->prev = base;
		newblock->next = base->next;
		if (newblock->next)
			newblock->next->prev = newblock;
		else
			mainzone->rover3 = newblock;

		base->next = newblock;
		base->size = size;

		newblock->size = extra;
		newblock->user = NULL; /* free block */
		newblock->tag = 0;
		newblock->id = ZONEID;
	}

	if (user) {
		base->user = user; /* mark as an in use block */
		*(void **)user = (void *)((uint8_t *)base + sizeof(memblock_t));
	} else {
		if (tag >= PU_PURGELEVEL)
			I_Error("an owner is required for purgable blocks");
		base->user = (void *)1; /* mark as in use, but unowned	 */
	}

	base->tag = tag;
	base->id = ZONEID;
	base->lockframe = NextFrameIdx;
	base->gfxcache = (void*)retaddr;

	mainzone->rover = base->next; /* next allocation will start looking here */
	if (!mainzone->rover) {
		mainzone->rover3 = base;
		mainzone->rover = mainzone->rover2;
	}

	Z_CheckZone(mainzone); /* DEBUG */

	return (void *)((uint8_t *)base + sizeof(memblock_t));
}

/*
========================
=
= Z_Alloc2
=
= You can pass a NULL user if the tag is < PU_PURGELEVEL
= Exclusive Psx Doom
========================
*/

void *__Z_Alloc2(memzone_t *mainzone, int size, int tag, void *user, uintptr_t retaddr, const char *file, int line)
{
	int extra;
	memblock_t *rover, *base, *block, *newblock;

	dbgio_printf("%s %s:%d\n", __func__, file, line);

	Z_CheckZone(mainzone);

	/* */
	/* scan through the block list looking for the first free block */
	/* of sufficient size, throwing out any purgable blocks along the way */
	/* */

	size += sizeof(memblock_t); /* account for size of block header */
	size = BLOCKALIGN(size,32); /* phrase align everything */

	base = mainzone->rover3;

	if (!base)
		I_Error("NULL mainzone->rover3");

	while (base->user || base->size < size) {
		if (base->user)
			rover = base;
		else {
			/* hit an in use block, so move base past it */
			rover = base->prev;
			if (!rover) {
				Z_DumpHeap(mainzone);
				I_Error("failed allocation on %i", size);
			}
		}

		if (rover->user) {
			if (!(rover->tag & PU_PURGELEVEL)) {
				if (!(rover->tag & PU_CACHE) || (uint32_t)rover->lockframe >= (NextFrameIdx - 1)) {
					/* hit an in use block, so move base past it */
					base = rover->prev;
					if (!base) {
						Z_DumpHeap(mainzone);
						I_Error("failed allocation on %i", size);
					}
					continue;
				}
			}

			/* */
			/* free the rover block (adding the size to base) */
			/* */
			Z_Free((uint8_t *)rover + sizeof(memblock_t)); /* mark as free */
		}

		if (base != rover) {
			/* merge with base */
			rover->size += base->size;
			rover->next = base->next;

			if (base->next)
				base->next->prev = rover;
			else
				mainzone->rover3 = rover;

			base = rover;
		}
	}

	/* */
	/* found a block big enough */
	/* */
	extra = (base->size - size);

	newblock = base;
	block = base;

	if (extra > MINFRAGMENT) {
		block = (memblock_t *)((uint8_t *)base + extra);
		block->prev = newblock;
		block->next = (void *)newblock->next;

		if (newblock->next)
			newblock->next->prev = block;

		newblock->next = block;
		block->size = size;
		newblock->size = extra;
		newblock->user = 0;
		newblock->tag = 0;
		newblock->id = ZONEID;
	}

	if (block->next == 0)
		mainzone->rover3 = block;

	if (user) {
		block->user = user; /* mark as an in use block */
		*(void **)user = (void *)((uint8_t *)block + sizeof(memblock_t));
	} else {
		if (tag >= PU_PURGELEVEL)
			I_Error("an owner is required for purgable blocks");
		block->user = (void *)1; /* mark as in use, but unowned	 */
	}

	block->id = ZONEID;
	block->tag = tag;
	block->lockframe = NextFrameIdx;
	base->gfxcache = (void*)retaddr;

	Z_CheckZone(mainzone);

	return (void *)((uint8_t *)block + sizeof(memblock_t));
}

/*
========================
=
= Z_Free2
=
========================
*/

void __Z_Free2(memzone_t *mainzone, void *ptr, const char *file, int line)
{
	(void)mainzone;
	memblock_t *block;

	dbgio_printf("%s %s:%d\n", __func__, file, line);

	block = (memblock_t *)((uint8_t *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID)
		I_Error("freed a pointer without ZONEID");

	if (block->user > (void **)0x100) /* smaller values are not pointers */
		*block->user = 0; /* clear the user's mark */
	block->user = NULL; /* mark as free */
	block->tag = 0;
}

/*
========================
=
= Z_FreeTags
=
========================
*/

void __Z_FreeTags(memzone_t *mainzone, int tag, const char *file, int line)
{
	memblock_t *block, *next;

	dbgio_printf("%s %s:%d\n", __func__, file, line);

	for (block = &mainzone->blocklist; block; block = next) {
		/* get link before freeing */
		next = block->next;

		/* free block */
		if (block->user) {
			if (block->tag & tag) {
				Z_Free((uint8_t *)block + sizeof(memblock_t));
			}
		}
	}

	for (block = &mainzone->blocklist; block; block = next) {
		/* get link before freeing */
		next = block->next;

		if (!block->user && next && !next->user) {
			block->size += next->size;
			block->next = next->next;
			if (next->next)
				next->next->prev = block;
			next = block;
		}
	}

	mainzone->rover = &mainzone->blocklist;
	mainzone->rover2 = &mainzone->blocklist;
	mainzone->rover3 = &mainzone->blocklist;

	block = mainzone->blocklist.next;
	while (block) {
		mainzone->rover3 = block;
		block = block->next;
	}
}

/*
========================
=
= Z_Touch
= Exclusive Doom64
=
========================
*/

extern int last_touched;
void __Z_Touch(void *ptr, const char *file, int line)
{
	memblock_t *block;

#if 0
	dbgio_printf("%s %s:%d\n", __func__, file, line);
#endif

	block = (memblock_t *)((uint8_t *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID) {
		uint32_t *block32 = (uint32_t *)block;
		dbgio_printf("INVALID ZONE ON TOUCH %d\n", last_touched);
		for (int i = 0; i < sizeof(memblock_t)/4; i++) {
			dbgio_printf("%08lx ", block32[i]);
		}
		dbgio_printf("\n");

		I_Error("touched a pointer to %d without ZONEID", last_touched);
	}

	block->lockframe = NextFrameIdx;
}

/*
========================
=
= Z_CheckZone
=
========================
*/

void __Z_CheckZone(memzone_t *mainzone, const char *file, int line)
{
	memblock_t *checkblock;
	int size;

	dbgio_printf("%s %s:%d\n", __func__, file, line);

	for (checkblock = &mainzone->blocklist; checkblock;
		 checkblock = checkblock->next) {

		if (checkblock->id != ZONEID) {
			Z_DumpHeap(mainzone);
			dbgio_printf("checkblock->id %08x\n", checkblock->id);
			I_Error("block missing ZONEID");
		}
		if (!checkblock->next) {
			size = (uint8_t *)checkblock + checkblock->size - (uint8_t *)mainzone;
			if (size != mainzone->size) {
				Z_DumpHeap(mainzone);
				I_Error("zone size changed from %d to %d\n", mainzone->size, size);
			}
			break;
		}

		if ((uint8_t *)checkblock + checkblock->size != (uint8_t *)checkblock->next) {
			Z_DumpHeap(mainzone);
			I_Error("block size does not touch the next block\n");
		}
		if (checkblock->next->prev != checkblock) {
			Z_DumpHeap(mainzone);
			I_Error("next block doesn't have proper back link\n");
		}
	}
}

/*
========================
=
= Z_ChangeTag
=
========================
*/

void __Z_ChangeTag(void *ptr, int tag, const char *file, int line)
{
	memblock_t *block;

	dbgio_printf("%s %s:%d\n", __func__, file, line);

	block = (memblock_t *)((uint8_t *)ptr - sizeof(memblock_t));
	if (block->id != ZONEID)
		I_Error("freed a pointer without ZONEID");
	if (tag >= PU_PURGELEVEL && (int)block->user < 0x100)
		I_Error("an owner is required for purgable blocks");
	block->tag = tag;
	block->lockframe = NextFrameIdx;
}

/*
========================
=
= Z_FreeMemory
=
========================
*/

int Z_FreeMemory(memzone_t *mainzone)
{
	memblock_t *block;
	int free;

	free = 0;
	for (block = &mainzone->blocklist; block; block = block->next) {
		if (!block->user)
			free += block->size;
	}

	return free;
}

/*
========================
=
= Z_DumpHeap
=
========================
*/

void Z_DumpHeap(memzone_t *mainzone)
{
	memblock_t *block;

	dbgio_printf("zone size: %i  location: %p\n", mainzone->size, mainzone);

	for (block = &mainzone->blocklist; block; block = block->next) {
		dbgio_printf("block:%p	id:%8i	size:%7i	user:%p	tag:%3i	frame:%i	retaddr:%08x\n",
			   block, block->id, block->size, block->user, block->tag,
			   block->lockframe, (uintptr_t)block->gfxcache);

		if (!block->next)
			continue;

		if ((uint8_t *)block + block->size != (uint8_t *)block->next)
			dbgio_printf("ERROR: block size does not touch the next block\n");
		if (block->next->prev != block)
			dbgio_printf("ERROR: next block doesn't have proper back link\n");
	}
}

/*
========================
=
= Z_Defragment
= Merges adjacent free blocks in the memory zone
=
========================
*/

void Z_Defragment(memzone_t *zone)
{
	memblock_t *block, *next;

	if (!zone)
		I_Error("Null zone heap");

	Z_CheckZone(zone);
	dbgio_printf("before:\n");
	Z_DumpHeap(zone);

	// Start with the first block
	block = &zone->blocklist;

	while (block) {
		next = block->next;

		// If the current block and the next block are both free
		if (!block->user && next && !next->user) {
			// Merge the blocks
			block->size += next->size;
			block->next = next->next;

			if (next->next) {
				next->next->prev = block;
			} else {
				// Update rover3 if we've merged with the last block
				zone->rover3 = block;
			}

			// Do not advance to the next block to handle chained merges
			continue;
		}

		// Move to the next block
		block = next;
	}

	// Reset rovers to avoid any dangling pointers
	zone->rover = &zone->blocklist;
	zone->rover2 = &zone->blocklist;
	zone->rover3 = &zone->blocklist;

	// Update rover3 to the last block in the list
	block = &zone->blocklist;
	while (block->next) {
		block = block->next;
		zone->rover3 = block;
	}

	Z_CheckZone(zone);
	dbgio_printf("after:\n");
	Z_DumpHeap(zone);
}

#endif