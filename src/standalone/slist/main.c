#include "list.h"

#include <stdlib.h>

int main()
{
    /*  This is called a sentinel node. It's not really a part of the list,
     *  it's just used to hold on to the list to be able to avoid certain
     *  special cases in the list implementation, and to make the solution
     *  slightly eaiser.
     */
	struct list_item root;
	root.value = -1;
	root.next = NULL;
	struct list_item* rootPtr = &root;


	/* Write your test cases here */
	append(rootPtr, 50);
	append(rootPtr, 80);
	append(rootPtr, 110);
	print(rootPtr);

	prepend(rootPtr, 40);
	prepend(rootPtr, 20);
	prepend(rootPtr, 10);
	print(rootPtr);

	input_sorted(rootPtr, 45);
	input_sorted(rootPtr, 120);
	input_sorted(rootPtr, 85);
	print(rootPtr);

	clear(rootPtr);
	print(rootPtr);

	return 0;
}
