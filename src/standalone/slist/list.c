#include <stdio.h>
#include <stdlib.h>

#include "list.h"

// Constructor
struct list_item* create_list_item(int init_value) {
    struct list_item* newNode = malloc(sizeof(struct list_item));
    newNode->value = init_value;
    newNode->next = NULL;
    return newNode;
}

void append(struct list_item *first, int x) {
    struct list_item* temp = first;
    while (temp->next != NULL) {
        temp = temp->next;
    }

    // Create new node and add it at the end of the list
    struct list_item* newNode = create_list_item(x);
    temp->next = newNode;
}
 
void prepend(struct list_item *first, int x) {
    struct list_item* newNode = create_list_item(x);
    newNode->next = first->next;
    first->next = newNode;
}


void input_sorted(struct list_item *first, int x) {
    struct list_item* temp = first;   // Ignore the first node (dummy node)
    struct list_item* newNode = create_list_item(x);
    while (temp->next != NULL) {
        if (temp->next->value > x) {
            // Add new node inbetween temp and temp->next
            newNode->next = temp->next;
            temp->next = newNode;
            return;
        }
        temp = temp->next;
    }

    // If we get here, the node is the largest. Add it to the end
    temp->next = newNode;
}

void print(struct list_item *first) {
    struct list_item* temp = first;
    while (temp != NULL) {
        printf("%d -> ", temp->value);
        temp = temp->next;
    }
    printf("\n");
}

void clear(struct list_item *first) {
    struct list_item* temp = first->next;
    struct list_item* oldTemp;

    while (temp->next != NULL) {
        // Set temp to the next node and free the old temp
        oldTemp = temp;
        temp = temp->next;
        free(oldTemp);
    }

    // Free the last node in the list
    free(temp);

    // Finally make the dummy node point to NULL
    // Don't call free() since its statically allocated in main
    first->next = NULL;
}

