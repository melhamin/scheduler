#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "linked_list.h"

// Util functions
node_t *create_node(void *data, int size)
{
    // Allocate memory for new node
    node_t *newItem = malloc(sizeof(node_t));
    // Allocate memory for node data
    newItem->data = malloc(size);
    memcpy(newItem->data, data, size);
    newItem->next = NULL;
    newItem->prev = NULL;
    return newItem;
}

// Destroy the node and free memory
void free_node(node_t *node)
{
    free(node->data);
    free(node);
}

/* List implementation */
LinkedList *list_init(int data_size)
{
    LinkedList *list = malloc(sizeof(LinkedList));

    if (list == NULL)
    {
        fprintf(stderr, "Failed to create the list!\n");
        exit(EXIT_FAILURE);
    }
    list->data_size = data_size;
    list->head = list->tail = NULL;
    list->size = 0;
    return list;
}

void list_add(LinkedList *list, void *data)
{
    node_t *newNode = create_node(data, list->data_size);
    if (newNode == NULL)
    {
        fprintf(stderr, "Failed to create new node!\n");
        exit(EXIT_FAILURE);
    }

    if (list->head == NULL)
        list->head = list->tail = newNode;
    else
    {
        node_t *curr_tail = list->tail;
        curr_tail->next = newNode;
        newNode->prev = curr_tail;
        list->tail = newNode;
    }
    list->size += 1;
}

void list_remove(LinkedList *list, node_t *item)
{
    if (list->head == NULL || item == NULL)
        return;
    if (list->head == item)
        list->head = item->next;
    if (item->next != NULL)
        item->next->prev = item->prev;
    if (item->prev != NULL)
        item->prev->next = item->next;

    free_node(item);
    list->size -= 1;
    return;
}

void list_remove_head(LinkedList *list)
{
    node_t *curr_head = list->head;
    if (curr_head)
    {
        if (curr_head == list->tail)
            list->head = list->tail = NULL;
        else
            list->head = curr_head->next;
        free_node(curr_head);
        list->size -= 1;
    }
}

void *list_get_first(LinkedList *list)
{
    return list->head;
    // void *result;
    // if (!list->head)
    //     result = NULL;
    // else
    //     result = list->head->data;
    // return result;
}

void list_destroy(LinkedList *list)
{
    node_t *curr = list->head;
    while (curr)
    {
        free_node(curr);
        curr = curr->next;
    }
}
