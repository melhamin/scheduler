#pragma once

// Structure of a node.
typedef struct Node
{
    void *data;
    struct Node *next;
    struct Node *prev;
} node_t;

// Structure of the linked list
typedef struct LinkedList
{
    int data_size;
    node_t *head;
    node_t *tail;
    int size;
} LinkedList;

// Initializes the list
LinkedList *list_init(int data_size);

// Adds the item to the end of the list
void list_add(LinkedList *list, void *);

void list_remove(LinkedList *list, node_t *item);

// Removes the last element
void list_remove_head(LinkedList *list);

// Removes from the beginning
void list_remove_tail(LinkedList *list);

// Returns the firs element in the list
void *list_get_first(LinkedList *list);

// Destroys the list
void list_destroy(LinkedList *list);
