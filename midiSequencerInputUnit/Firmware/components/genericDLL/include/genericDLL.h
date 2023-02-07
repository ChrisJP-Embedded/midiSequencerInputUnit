#include "sequencerGrid.h"

//This module provides functions to manage a double-linked-list
//The NODE_TYPE must be updated to the same type that mades up the nodes in the list.

//IMPORTANT: The module assumes that each node object has the following members:
//NODE_TYPE.nextPtr
//NODE_TYPE.prevPtr

#define NODE_TYPE SequencerGridItem_t

//IMPORTANT: This module also assumes that the caller has both list HEAD and TAIL
//access to both a HEAD and TAIL pointer for the list.

//NOTE: This module does not provide any double-linked-list data structure,
//its simply a group of helper functions for management of a double linked list.

NODE_TYPE * genericDLL_createNewNode(void);
void genericDLL_appendNewNodeOntoLinkedList(NODE_TYPE * newNodePtr, NODE_TYPE ** listHeadPtr, NODE_TYPE ** listTailPtr);
void genericDLL_insertNewNodeIntoLinkedList(NODE_TYPE * newNodePtr, NODE_TYPE * insertLocationPtr, NODE_TYPE ** listHeadPtr);
void genericDLL_freeEntireLinkedList(NODE_TYPE ** listHeadPtr, NODE_TYPE ** listTailPtr);
void genericDLL_deleteNodeFromList(NODE_TYPE * deleteNodePtr, NODE_TYPE ** listHeadPtr, NODE_TYPE ** listTailPtr);
bool genericDLL_returnTrueIfFirstNodeInList(NODE_TYPE * nodePtr);
bool genericDLL_returnTrueIfLastNodeInList(NODE_TYPE * nodePtr);
