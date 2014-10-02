#define INVERTER_NODE_SIZE	1008

struct list_node{
	char data[INVERTER_NODE_SIZE];
	struct list_node *next;
	struct list_node *prev;
};

struct list_node* list_extend(struct list_node *);
void list_trunc(struct list_node *);
