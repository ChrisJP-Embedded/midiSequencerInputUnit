#include <stdio.h>
#include <stdbool.h>
#include <esp_log.h>
#include "esp_heap_caps.h"
#include "malloc.h"
#include "memory.h"
#include "genericDLL.h"


static inline void freeNode(NODE_TYPE * nodePtr);


struct moduleData {

    bool moduleInitialized;
    uint32_t totalNodes;
    uint32_t currNodeIdx;
    NODE_TYPE ** nodeArrPtr;
} g_GenericDLLData;



//---- Public
void genericDLL_init(uint32_t numberNodes)
{
    assert(g_GenericDLLData.moduleInitialized == false);

    if(g_GenericDLLData.moduleInitialized == false)
    {
        //This only runs once, at system startup. No dynamic allocation at runtime.
        g_GenericDLLData.nodeArrPtr = heap_caps_calloc(numberNodes, sizeof(**g_GenericDLLData.nodeArrPtr), MALLOC_CAP_SPIRAM);
        assert(g_GenericDLLData.nodeArrPtr != NULL);
        g_GenericDLLData.totalNodes = numberNodes;
        g_GenericDLLData.currNodeIdx = 0;
        g_GenericDLLData.moduleInitialized = true;
    }
}


//---- Public
NODE_TYPE * genericDLL_createNewNode(void)
{
    assert(g_GenericDLLData.moduleInitialized == true);

    //This function doesnt actually create node, it just returns
    //a pointer to the next unused node in the pre-allocated node 
    //pool that was create at system startup.
    //This methods allows us to avoid dynamic memory allocation
    //during runtime as its all done at startup.

    NODE_TYPE * nodePtr = NULL;

    if(g_GenericDLLData.currNodeIdx < (g_GenericDLLData.totalNodes - 1))
    {
        assert(g_GenericDLLData.nodeArrPtr[g_GenericDLLData.currNodeIdx] != NULL);
        nodePtr = g_GenericDLLData.nodeArrPtr[g_GenericDLLData.currNodeIdx];
        g_GenericDLLData.nodeArrPtr[g_GenericDLLData.currNodeIdx++] = NULL;
    }
    else
    {
        //TODO: ADD OUT OF NODES HANDLING
        //BUT SHOULDNT GET HERE ANYWAY
        assert(0);
    }

    return nodePtr;
}


//---- Public
void genericDLL_appendNewNodeOntoLinkedList(NODE_TYPE * newNodePtr, NODE_TYPE ** listHeadPtr, NODE_TYPE ** listTailPtr)
{
    assert(g_GenericDLLData.moduleInitialized == true);
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
    assert(g_GenericDLLData.moduleInitialized == true);
    assert(newNodePtr != NULL);
    assert(listHeadPtr != NULL);

    //IMPORTANT:
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
    assert(g_GenericDLLData.moduleInitialized == false);
    assert(*listHeadPtr != NULL);
    assert(*listTailPtr != NULL);

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
                freeNode(nodePtr->prevPtr);
                moreNodesToDelete = true;
            } 
            else
            {
                freeNode(nodePtr);
            }

        }while(moreNodesToDelete);
    }
}


//---- Public
void genericDLL_deleteNodeFromList(NODE_TYPE * deleteNodePtr, NODE_TYPE ** listHeadPtr, NODE_TYPE ** listTailPtr)
{
    assert(g_GenericDLLData.moduleInitialized == true);
    assert(deleteNodePtr != NULL);
    assert(*listHeadPtr != NULL);
    assert(*listTailPtr != NULL);


    NODE_TYPE * nextNodePtr = NULL;
    NODE_TYPE * prevNodePtr = NULL;

    if(deleteNodePtr == *listHeadPtr)
    {
        if((*listHeadPtr)->nextPtr != NULL)
        {
            *listHeadPtr = (*listHeadPtr)->nextPtr;
            (*listHeadPtr)->prevPtr = NULL;
        }
        else
        {
            *listHeadPtr = NULL;
            *listTailPtr = NULL;
        }
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

    freeNode(deleteNodePtr);
}


//---- Private 
static inline void freeNode(NODE_TYPE * nodePtr)
{
    assert(g_GenericDLLData.moduleInitialized == true);
    assert(nodePtr != NULL);

    memset(nodePtr, 0, sizeof(*nodePtr));
    memset(nodePtr->dataBytes, 0, MAX_DATA_BYTES);

    if(g_GenericDLLData.currNodeIdx > 0)
    {
        --g_GenericDLLData.currNodeIdx;
        assert(g_GenericDLLData.nodeArrPtr[g_GenericDLLData.currNodeIdx] == NULL);
        g_GenericDLLData.nodeArrPtr[g_GenericDLLData.currNodeIdx] = nodePtr;
    }
    else
    {
        //TODO: ADD OUT OF NODES HANDLING
        //BUT SHOULDNT GET HERE ANYWAY
        assert(0);
    }
}