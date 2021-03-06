////////////////////////////////////////////////////////
// Jonathan Jones - jonesjon 932709446
// Concurrency 3
// CS444 Spring2018
////////////////////////////////////////////////////////

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include "mt19937ar.h"

typedef struct Node {
	int value;
	struct Node* next;
}Node;

//Constructed from equivalent python implementation in little book of semaphores page 70
typedef struct Lightswitch { 
	int counter;
	sem_t* mutex;
}Lightswitch;

void ls_lock(Lightswitch* ls, sem_t* s)
{
	sem_wait(ls->mutex);
	ls->counter += 1;
	if(ls->counter == 1)
		sem_wait(s);
	sem_post(ls->mutex);
}

void ls_unlock(Lightswitch* ls, sem_t* s)
{
	sem_wait(ls->mutex);
	ls->counter -= 1;
	if(ls->counter == 0)
		sem_post(s);
	sem_post(ls->mutex);
}

//Arguments for the search thread
typedef struct Searcher_args {
	Node** list;
	Lightswitch* search_switch; 
	sem_t* no_search;
	sem_t* talk;
}Searcher_args;

//Arguments for the insertion threads
typedef struct Inserter_args {
	Node** list;
	Lightswitch* insert_switch;
	sem_t* insert_mutex;
	sem_t* no_insert;
	sem_t* talk;
}Inserter_args;

//Arguments for the deleter threads
typedef struct Deleter_args {
	Node** list;
	sem_t* no_search;
	sem_t* no_insert;
	sem_t* talk;
}Deleter_args;

int bit;

//Function prototypes
unsigned int prng();
void driver();
void show_list(Node*); //Searcher function
void insert(Node**, int); //Inserter function
void delete(Node**, int); //Deleter function
void delete_end(Node**);
void free_list(Node**);

void* searcher(void*);
void* inserter(void*);
void* deleter(void*);

pthread_t* get_threads(int, void*, void*);

int main()
{
	unsigned int eax;
    unsigned int ebx;
    unsigned int ecx;
    unsigned int edx;

    eax = 0x01;

    //Gets details about the processor chip (So we can check which psuedo random number generator it supports)
    //Communicates directly with the operating system for information
    //The "=a", "=b", "=c", "=d" tells the system to output the eax, ebx, ecx and edx register values after computation into the c containers we gave it
    __asm__ __volatile__(
        "cpuid;"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) //Send the outputs of the registers to our c values
        : "a"(eax) //Eax value will be the input (The 0x01 probably specifies that we want whatever information cpuid gives us from an argument of 1)
    );

    if (ecx & 0x40000000) 
    {
        printf("Using rdrand\n");
        bit = 1; //Use rdrand for INTEL processor chips
    }
    else {
        init_genrand(time(NULL)); //Seed the mt19937 random value generator with time(NULL)
        bit = 0; //Use mt19937 for the ENGR server and other processor chips
    }

    //Run main program code
    driver();

    return 0;
}

/*************************************************
 * Function: Runs the main program code
 * Description: Sets up the semaphores and the threads for execution
 * Params: none
 * Returns: none
 * Pre-conditions: bit is set so prng knows what random number generator to use 
 * Post-conditions: none
 * **********************************************/
void driver()
{
	//Initialize constructs for problem
	Node* head = NULL;

	Lightswitch search_switch, insert_switch;
	sem_t *insert_mutex, *no_search, *no_insert, *talk;

	insert_mutex = (sem_t*)malloc(sizeof(sem_t));
	no_search = (sem_t*)malloc(sizeof(sem_t));
	no_insert = (sem_t*)malloc(sizeof(sem_t));
	talk = (sem_t*)malloc(sizeof(sem_t));

	search_switch.counter = 0;
	insert_switch.counter = 0;

	search_switch.mutex = (sem_t*)malloc(sizeof(sem_t));
	insert_switch.mutex = (sem_t*)malloc(sizeof(sem_t));

	//Initialize semaphores
	sem_init(insert_mutex, 0, 1);
	sem_init(no_search, 0, 1);
	sem_init(no_insert, 0, 1);
	sem_init(search_switch.mutex, 0, 1);
	sem_init(insert_switch.mutex, 0, 1);
	sem_init(talk, 0, 1);

	//Initialize arguments
	Searcher_args s_arg; 
	Inserter_args i_arg; 
	Deleter_args d_arg;
	s_arg.list = &head; s_arg.talk = talk; 
	i_arg.list = &head; i_arg.talk = talk;
	d_arg.list = &head; d_arg.talk = talk;
	s_arg.search_switch = &search_switch;
	s_arg.no_search = no_search;
	i_arg.insert_switch = &insert_switch;
	i_arg.insert_mutex = insert_mutex;
	i_arg.no_insert = no_insert;
	d_arg.no_search = no_search;
	d_arg.no_insert = no_insert;

	//Initialize threads
	pthread_t *searchers, *inserters, *deleters;

	//Get between 1 and 5 of each thread type
	int num_searchers = prng()%5 + 1;
	int num_inserters = prng()%5 + 1;
	int num_deleters = prng()%5 + 1;

	printf("Searchers: %d\tInserters: %d\tDeleters: %d\nThread execution will begin in 5 seconds...\n", num_searchers, num_inserters, num_deleters);
	sleep(5);

	searchers = get_threads(num_searchers, searcher, &s_arg);
	inserters = get_threads(num_inserters, inserter, &i_arg);
	deleters = get_threads(num_deleters, deleter, &d_arg);

	pthread_join(searchers[0], NULL); //Have the parent thread wait forever
	return;
}

/*************************************************
 * Function: get_threads
 * Description: Creates and returns a certain number of threads given the function and arguments to use
 * Params: number of threads (greater than 0), void* function for the thread to use, arguments to that function in a structure
 * Returns: List of pthreads.
 * Pre-conditions: Valid arguments passed in, non-negative num_threads
 * Post-conditions: Threads are initialized properly and executing.
 * **********************************************/
pthread_t* get_threads(int num_threads, void* function, void* args)
{
	pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t)*num_threads);
	int i; for(i = 0; i < num_threads; i++)
	{
		pthread_create(&(threads[i]), NULL, function, args);
	}
	return threads;
}

/*************************************************
 * Function: searcher
 * Description: Thread function for searcher threads. It will print each item from the linked list (Basically a search of the whole list) when there are no deleters in use
 * Params: Searcher_args pointer structure.
 * Returns: None
 * Pre-conditions: Arguments structure is properly filled out
 * Post-conditions: None
 * **********************************************/
void* searcher(void* args)
{
	Searcher_args* s_arg = (Searcher_args*)(args); //Get arguments into proper structure for usage
	int id = pthread_self(); //Get id for informative prints
	while(1)
	{
		//Enforce semaphore for STDOUT usage
		sem_wait(s_arg->talk);
		printf("[SEARCH-WAIT] Thread 0x%x is checking for active delete threads.\n", id);
		sem_post(s_arg->talk);
		ls_lock(s_arg->search_switch, s_arg->no_search); //Flip the lightswitch for searchers if first thread

		sem_wait(s_arg->talk);
		printf("[SEARCH-ACTION] Thread 0x%x is searching the list.\nList: ", id);
		show_list(*(s_arg->list)); //Search through the linked list
		sem_post(s_arg->talk);

		sleep(prng()%3+1);

		ls_unlock(s_arg->search_switch, s_arg->no_search); //Flip the lightswitch for searchers if last thread
		sleep(prng()%10+1); //Sleep for awhile before next attempt to search
	}
	return;
}

/*************************************************
 * Function: inserter
 * Description: Thread function for inserter threads. It will insert an item onto the end of the linked list when there are no other inserters or deleters in use
 * Params: Inserter_args pointer structure
 * Returns: None
 * Pre-conditions: Arguments structure is properly filled out
 * Post-conditions: 
 * **********************************************/
void* inserter(void* args)
{
	Inserter_args* i_arg = (Inserter_args*)(args);
	int val;
	int id = pthread_self();
	while(1)
	{
		val = prng()%101; //Random value to add to the list
		sem_wait(i_arg->talk);
		printf("[INSERT-WAIT] Thread 0x%x is checking for active insert and delete threads.\n", id);
		sem_post(i_arg->talk);
		ls_lock(i_arg->insert_switch, i_arg->no_insert); //Flip the lightswitch for inserters if first thread
		sem_wait(i_arg->insert_mutex);

		insert(i_arg->list, val); //Insert into the list
		sem_wait(i_arg->talk);
		printf("[INSERT-ACTION] Thread: 0x%x inserted %d into the list.\n", id, val);
		sem_post(i_arg->talk);

		sleep(prng()%3+1); //Sleep between 1 and 3 seconds for insertion time

		sem_post(i_arg->insert_mutex);
		ls_unlock(i_arg->insert_switch, i_arg->no_insert); //Flip the lightswitch for inserters if last thread
		sleep(prng()%10+1); //Sleep for awhile before next attempt to insert

	}
	return;
}

/*************************************************
 * Function: deleter
 * Description: Thread function for deleter threads. Deletes an item from the end of the list if there are no searchers, deleters or inserter threads currently active (or queued to wait before)
 * Params: Deleter_args pointer structure
 * Returns: None
 * Pre-conditions: Arguments is properly filled out
 * Post-conditions: None
 * **********************************************/
void* deleter(void* args)
{
	Deleter_args* d_arg = (Deleter_args*)(args);
	int id = pthread_self();
	while(1)
	{
		sem_wait(d_arg->talk);
		printf("[DELETE-WAIT] Thread 0x%x is checking for active search, insert and delete threads.\n", id);
		sem_post(d_arg->talk);
		sem_wait(d_arg->no_search); //Any searchers?
		sem_wait(d_arg->no_insert); //Any deleters?

		delete_end(d_arg->list); //Delete item from end of the list
		sem_wait(d_arg->talk);
		printf("[DELETE-ACTION] Thread 0x%x deleted end of list.\n", id);
		sem_post(d_arg->talk);
		sleep(prng()%3+1); //Sleep between 1 and 3 seconds during deletion

		sem_post(d_arg->no_insert);
		sem_post(d_arg->no_search);
		sleep(prng()%10+1); //Sleep between 1 and 10 seconds before trying to delete again
	}
	return;
}

/*************************************************
 * Function: show_list
 * Description: Prints out the contents of a linked list. Used by the searcher thread to simulate searching (Reads).
 * Params: Linked list node pointer (First call should be the head if you want to print out entire list)
 * Returns: None
 * Pre-conditions: None 
 * Post-conditions: Linked list is printed out or if list is empty a newline is printed
 * **********************************************/
void show_list(Node* node)
{
	if(node != NULL)
	{
		printf("%d, ", node->value);
		show_list(node->next);
		return;
	}
	printf("\n");
}

/*************************************************
 * Function: insert
 * Description: Inserts a node onto the end of a singly linked list. Creates a head if the list is empty.
 * Params: Pointer to Linked List node pointer (This is so that if the list is empty, a head can be created)
 *		   Integer value to be inserted as new node.
 * Returns: None
 * Pre-conditions: None 
 * Post-conditions: New node is allocated memory and added to the linked list
 * **********************************************/
void insert(Node** node, int value)
{
	if(*node == NULL)
	{
		*node = (Node*)malloc(sizeof(Node));
		(*node)->value = value;
		(*node)->next = NULL;
		return;
	}
	insert(&(*node)->next, value);
}

/*************************************************
 * Function: delete
 * Description: Deletes the node in the linked list corresponding to the value passed in. Deallocates node deleted.
 * Params: Pointer to Linked List node pointer (This is needed if the head will be deleted)
 *         Integer value corresponding to the node you want deleted from the list.
 * Returns: None
 * Pre-conditions: None 
 * Post-conditions: Node corresponding to value has been removed and deallocated or no change if value is not present in list.
 *                  If head node is deleted, new head is updated or given NULL value.
 * **********************************************/
void delete(Node** node, int value)
{
	if(*node == NULL) //If list is empty or end of list reached
		return;
	
	if((*node)->next != NULL)
	{
		if((*node)->next->value == value) //If the next node over has the value we want
		{
			Node* temp = (*node)->next; //Temporarily store the location of the node we're removing
			(*node)->next = temp->next; //Direct current node to whatever follows the deleted node
			free(temp); //Free the memory that temp points to
			return;
		}
	}
	else if((*node)->value == value) //If current node we're at is the head
	{
		//Set the new head and free the current head
		Node* temp = (*node)->next;
		*node = (*node)->next;
		free(temp);
		return;
	}
	delete(&(*node)->next, value);
}

/*************************************************
 * Function: delete_end
 * Description: Deletes a node from the end of the linked list. Deallocates node deleted.
 * Params: Pointer to Linked List node pointer (This is needed if the head will be deleted)
 * Returns: None
 * Pre-conditions: None
 * Post-conditions: End of the linked list is deleted and deallocated.
 * **********************************************/
void delete_end(Node** node)
{
	if(*node == NULL) //Empty list
		return;

	if((*node)->next != NULL) //Is the next item null?
	{
		if((*node)->next->next == NULL) //Next item is the tail
		{
			free((*node)->next);
			(*node)->next = NULL;
			return;
		}
	}
	else { //We're the head of the list
		free(*node);
		*node = NULL;
		return;
	}
	delete_end(&(*node)->next);
}

/*************************************************
 * Function: free_list
 * Description: Deallocates all list items
 * Params: Pointer to Linked List node pointer (So we can set the values of the node pointers to NULL)
 * Returns: None
 * Pre-conditions: None 
 * Post-conditions: All nodes deallocated and set to NULL
 * **********************************************/
void free_list(Node** node)
{
	if((*node) == NULL)
		return;
	free_list((*node)->next);
	free(*node);
	*node = NULL;
}

/*************************************************
 * Function: prng
 * Description: Psuedo Random Number Genrator. INTEL CHIP: Uses the rdrand asm instruction to generate a random number. Loops until the instruction has successfully
 * returned a random number. OTHER CHIPS: Uses the mt19937ar.h file functions to generate a random number.
 * Params: None
 * Returns: Random unsigned int
 * Pre-conditions: Processor chip has been identified correctly and bit is set to either 0 or 1 respectively.
 * Post-conditions: None
 * **********************************************/
unsigned int prng()
{
    unsigned int rnd = 0;
    unsigned char ok = 0; //Used to check if rdrand failed or not

    //If bit is 0 then use mt19937 else use rdrand
    if(bit)
    {
        while(!((int)ok))
        {
            __asm__ __volatile__ (
                "rdrand %0; setc %1"
                : "=r" (rnd), "=qm" (ok) //Get a random number using rdrand
            );
        }
    }
    else
        rnd = (unsigned int)genrand_int32(); //Get a random number using mt19937

    return rnd;
}