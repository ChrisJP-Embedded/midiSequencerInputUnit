#include <stdio.h>
#include <stdbool.h>
#include "esp_heap_caps.h"
#include "malloc.h"
#include "memory.h"
#include "genericDLL.h"


//Generic double-linked-list helper fucntions, 
//see genericDLL.h for instructions on usage.


//---- Public
NODE_TYPE * genericDLL_createNewNode(void)
{
    NODE_TYPE * newNodePtr = heap_caps_malloc(sizeof(NODE_TYPE), MALLOC_CAP_SPIRAM);
    if(newNodePtr != NULL)
    {
        newNodePtr->nextPtr = NULL;
        newNodePtr->prevPtr = NULL;
    }
    return newNodePtr;
}


//---- Public
void genericDLL_appendNewNodeOntoLinkedList(NODE_TYPE * newNodePtr, NODE_TYPE ** listHeadPtr, NODE_TYPE ** listTailPtr)
{
    assert(newNodePtr != NULL);

    if(*listHeadPtr == NULL)
    {
        //This is the case where we are
        //appending a node to an empty list
        *listHeadPtr = newNodePtr;
        *listTailPtr = newNodePtr;
        newNodePtr->nextPtr = NULL;
        newNodePtr->prevPtr = NULL;
    }
    else
    {
        //This is the case where we are appending 
        //a node to a list with existing nodes
        assert(*listTailPtr != NULL);
        (*listTailPtr)->nextPtr = newNodePtr;
        newNodePtr->nextPtr = NULL;
        newNodePtr->prevPtr = *listTailPtr;
        *listTailPtr = newNodePtr;
    }
}


//---- Public
void genericDLL_insertNewNodeIntoLinkedList(NODE_TYPE * newNodePtr, NODE_TYPE * insertLocationPtr, NODE_TYPE ** listHeadPtr)
{
    assert((newNodePtr != NULL) && (listHeadPtr != NULL));


    //IMPORTANT:
    //IF(insertLocationPtr == NULL) The new event node will be inserted as the new HEAD of the list.
    //IF(insertLocationPtr != NULL) The new event node will be inserted at insertLocationPtr->nextPtr
    if(insertLocationPtr == NULL) 
    {
        newNodePtr->nextPtr = *listHeadPtr;
        newNodePtr->prevPtr = NULL;    
        (*listHeadPtr)->prevPtr = newNodePtr;
        *listHeadPtr = newNodePtr;
    }
    else
    { 
        //Insertion location is within body of linked list (where the insert 
        //location has existing nodes  on both sides). A NULL nextPtr here 
        //indicates that isnt the case, so its a system fault
        assert(insertLocationPtr->nextPtr != NULL);

        newNodePtr->nextPtr = insertLocationPtr->nextPtr;
        newNodePtr->prevPtr = insertLocationPtr;    
        insertLocationPtr->nextPtr->prevPtr = newNodePtr;
        insertLocationPtr->nextPtr = newNodePtr;
    }
}

inline bool genericDLL_returnTrueIfLastNodeInList(NODE_TYPE * nodePtr)
{
    assert(nodePtr != NULL);
    if(nodePtr->nextPtr == NULL) return true;
    else return false;
}

inline bool genericDLL_returnTrueIfFirstNodeInList(NODE_TYPE * nodePtr)
{
    assert(nodePtr != NULL);
    if(nodePtr->prevPtr == NULL) return true;
    else return false;
}


//---- Public
void genericDLL_freeEntireLinkedList(NODE_TYPE ** listHeadPtr, NODE_TYPE ** listTailPtr)
{
    NODE_TYPE * nodePtr = NULL;
    bool moreNodesToDelete;

    //The list tail ptr
    //is always NULL'd
    *listTailPtr = NULL;

    //If current linked list has any 
    //event nodes allocated, we need to delete them
    if(*listHeadPtr != NULL)
    {
        //Grab a direct pointer to the list head node
        nodePtr = *listHeadPtr;

        //We can now NULL the linked list head 
        //ptr as we're about to delete all nodes 
        *listHeadPtr = NULL;

        do 
        {
            moreNodesToDelete = false;

            if(nodePtr->nextPtr != NULL)
            {
                nodePtr = nodePtr->nextPtr;
                assert(nodePtr->prevPtr != NULL);
                free(nodePtr->prevPtr);
                moreNodesToDelete = true;
            } 
            else
            {
                free(nodePtr);
            }

        }while(moreNodesToDelete);
    }
}


//---- Public
void genericDLL_deleteNodeFromList(NODE_TYPE * deleteNodePtr, NODE_TYPE ** listHeadPtr, NODE_TYPE ** listTailPtr)
{
    assert((deleteNodePtr != NULL) && (*listHeadPtr != NULL) && (*listTailPtr != NULL));

    NODE_TYPE * nextNodePtr = NULL;
    NODE_TYPE * prevNodePtr = NULL;

    if(deleteNodePtr == *listHeadPtr)
    {
        *listHeadPtr = NULL;
        *listTailPtr = NULL;
    }
    else if(deleteNodePtr == *listTailPtr)
    {
        assert(deleteNodePtr->prevPtr != NULL);
        *listTailPtr = deleteNodePtr->prevPtr;
        (*listTailPtr)->nextPtr = NULL;
    }
    else
    {
        assert((deleteNodePtr->nextPtr != NULL) && (deleteNodePtr->prevPtr != NULL));
        nextNodePtr = deleteNodePtr->nextPtr;
        prevNodePtr = deleteNodePtr->prevPtr;
        nextNodePtr->prevPtr = prevNodePtr;
        prevNodePtr->nextPtr = nextNodePtr;
    }

    free(deleteNodePtr);
}