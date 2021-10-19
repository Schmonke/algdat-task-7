#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <sys/mman.h>

/* Edge represents connection between two nodes
 * Graph contains an array of nodes, and each node contains a linked-list of edges
 * where each edge says which node it points to.
 */

/**
 * Linked list containing all the edges that correspond to a node.
 */
typedef struct edge
{
    struct edge *next;
    struct edge *inverted;
    int dst_node_id;
    int capacity; //When full -> inverted is 0 and vice versa
    int flow;     //Difference between capacity and flow gives current capacity
} edge;

/**
 * Node containing a linked list of the edges connected to that node.
 */
typedef struct node
{
    struct edge *edges;

    // BFS
    bool visited;
    int prev_node_id;
    int dist;
} node;

const int QUEUE_DEFAULT_CAPACITY = 16;
typedef struct queue
{
    int capacity;
    int count;
    int write_index;
    int read_index;
    int *elems;
} queue;

/**
 * Graph containing all nodes read from file
 */
typedef struct graph
{
    node *nodes;
    int total_node_count; // includes special nodes
    int node_count;
    int edge_count;
    int super_source_node_id;
    int super_sink_node_id;
} graph;

/**
 * Constructor for a new edge
 */
edge *new_edge(int capacity, int dst_node_id, edge *next)
{
    edge *e = malloc(sizeof(edge));
    e->capacity = capacity;
    e->dst_node_id = dst_node_id;
    e->next = next;
    e->inverted = NULL;
    return e;
}

/**
 * Adds an edge to the front of the linked list.
 * Adds to front insted of back because its more 
 * efficient and the list order is irelevant. 
 */
edge *edge_add(edge **edges, int dst_node_id, int capacity)
{
    edge *e = new_edge(capacity, dst_node_id, *edges);
    *edges = e;
    return e;
}

/**
 * Function to count the edges in the graph
 */
int edge_count(node *n)
{
    int count = 0;
    for (edge *e = n->edges; e; e = e->next)
        count++;
    return count;
}

/**
 * Creates a new queue.
 */
queue *new_queue()
{
    queue *q = malloc(sizeof(queue));
    q->capacity = QUEUE_DEFAULT_CAPACITY;
    q->elems = malloc(sizeof(int) * QUEUE_DEFAULT_CAPACITY);
    q->count = 0;
    q->write_index = 0;
    q->read_index = 0;
    return q;
}

/**
 * Pushes a node_id onto the queue.
 */
void queue_push(queue *q, int node_id)
{
    if (q->count == q->capacity)
    {
        int old_capacity = q->capacity;
        int new_capacity = q->capacity * 2;
        int *new_elems = malloc(sizeof(int) * new_capacity);

        int cp = q->read_index;
        int t = 0;
        while (t != q->count)
        {
            new_elems[t] = q->elems[cp];
            cp = (cp + 1) % old_capacity;
            t++;
        }
        free(q->elems);

        q->capacity = new_capacity;
        q->elems = new_elems;
        q->read_index = 0;
        q->write_index = t;
    }
    q->elems[q->write_index] = node_id;
    q->write_index = (q->write_index + 1) % q->capacity;
    q->count++;
}

/**
 * Peeks the bottom-most (first) element on the queue.
 */
int queue_peek(queue *q, bool *found)
{
    if (q->count == 0)
    {
        if (found != NULL)
            *found = false;
        return 0;
    }
    if (found != NULL)
        *found = true;
    return q->elems[q->read_index];
}

/**
 * Removes and returns first value in the queue.
 * Shifts the head to point to next queue_node.  
 */
int queue_pop(queue *q, bool *found)
{
    bool f;
    int elem = queue_peek(q, &f);
    if (f)
    {
        q->read_index = (q->read_index + 1) % q->capacity;
        q->count--;
    }
    if (found != NULL)
        *found = f;
    return elem;
}

/**
 * Frees the memory of the queue before a new que is made.
 */
void queue_free(queue *q)
{
    free(q->elems);
    free(q);
}

void graph_populate_inverted_edges(graph *g)
{
    for (int i = 0; i < g->node_count; i++)
    {
        node *src_node = &g->nodes[i];
        for (edge *e = src_node->edges; e; e = e->next)
        {
            bool has_inverted = false;
            node *dst_node = &g->nodes[e->dst_node_id];
            for (edge *e = dst_node->edges; e; e = e->next)
            {
                if (e->dst_node_id == i)
                {
                    has_inverted = true;
                    break;
                }
            }

            if (!has_inverted)
            {
                edge *inv_edge = edge_add(&dst_node->edges, i, 0);
                printf("NODE %4d -> NODE %6d | INODE %6d -> INODE %6d\n", i, e->dst_node_id, e->dst_node_id, inv_edge->dst_node_id);
                e->inverted = inv_edge;
            }
        }
    }
}

void graph_create_super_source(graph *g)
{
    node *super_source = &g->nodes[g->super_source_node_id];
    for (int i = 0; i < g->node_count; i++)
    {
        node n = g->nodes[i];
        bool has_incoming_edges = false;
        for (edge *e = n.edges; e != NULL; e = e->next)
        {
            if (e->inverted == NULL)
            {
                has_incoming_edges = true;
                break;
            }
        }

        if (!has_incoming_edges)
        {
            edge_add(&super_source->edges, i, INT_MAX);
        }
    }
}

void graph_create_super_sink(graph *g)
{
    for (int i = 0; i < g->node_count; i++)
    {
        node *n = &g->nodes[i];
        bool has_only_incoming_edges = true;
        for (edge *e = n->edges; e != NULL; e = e->next)
        {
            if (e->inverted != NULL)
            {
                has_only_incoming_edges = false;
                break;
            }
        }

        if (has_only_incoming_edges)
        {
            edge_add(&n->edges, g->super_sink_node_id, INT_MAX);
        }
    }
}

/**
 * Resets the visited flags on the nodes before starting a search
 * with BFS or DFS/Topo Sort
 */
void graph_reset_flags(graph *g)
{
    for (int i = 0; i < g->total_node_count; i++)
    {
        g->nodes[i].visited = false;
        g->nodes[i].prev_node_id = -1;
        g->nodes[i].dist = -1;
    }
}
/**
 * Frees the memory allocated previously allocated from the graph
 */
void graph_free(graph *g)
{
    for (int i = 0; i < g->node_count; i++)
    {
        edge *e = g->nodes[i].edges;
        while (e)
        {
            edge *next = e->next;
            free(e);
            e = next;
        }
    }
    free(g->nodes);
    free(g);
}

/**
 * Use queue as holder of nodes to use. 
 * It represents the order the nodes are visited. 
 * 
 * Returns true if the super sink was reached.
 */
bool bfs(graph *g)
{
    graph_reset_flags(g);

    queue *q = new_queue();

    // push initial node onto queue for searching
    node *src = &g->nodes[g->super_source_node_id];
    src->visited = true;

    queue_push(q, g->super_source_node_id);

    // look over nodes
    while (true)
    {
        bool found;
        int n_id = (int)queue_pop(q, &found);
        if (!found)
            break;

        node *n = &g->nodes[n_id];
        n->visited = true;

        // look over edges and push to queue
        // TODO: check how much capacity is left :- )
        for (edge *e = n->edges; e; e = e->next) //Stops when all edges of a node are visited.
        {
            int targetNodeId = e->dst_node_id;
            node *target = &g->nodes[targetNodeId];

            if (target->visited)
                continue;

            target->prev_node_id = n_id;
            target->dist = n->dist + 1;
            queue_push(q, targetNodeId);
            target->visited = true;
        }
    }
    queue_free(q);

    return g->nodes[g->super_sink_node_id].visited;
}

void edmond_karp(graph *g)
{
    //Run BFS and log nodes used for the shortest path
    //Decrease capacity of all used edges in a run, by one.
    //When an edge's capacity is = 0 the original edge is no longer available.
    //If an edge's capacity is full, the inverted edge is not available.
    //When an edge's original capacity decrease, the inverted capacity increase
    // and vice versa.
    int max_flow = 0;

    node *sink = &g->nodes[g->super_sink_node_id];
    node *source = &g->nodes[g->super_source_node_id];

    while (bfs(g))
    {
        // Find maximum flow that can yoink through the path.
        int path_max_flow = INT_MAX;
        int node_id = sink->prev_node_id;
        for (node *n = sink; n != source; n = &g->nodes[n->prev_node_id])
        {
            // todo: make better pls
            for (edge *e = n->edges; e != NULL; e = e->next)
            {
                if (e->dst_node_id == node_id)
                {
                    if (e->capacity < path_max_flow)
                    {
                        path_max_flow = e->capacity;
                    }
                }
            }
            node_id = n->prev_node_id;
        }
        // !!update edge capacities!!

        // snacks snax
    }
}

/**
 * Prints the results of BFS.
 */
void print_bfs_result(graph *g)
{
    node super_source = g->nodes[g->super_source_node_id];
    node super_sink = g->nodes[g->super_sink_node_id];

    printf("%-6s | %-5s | %-5s\n", "Node", "Prev", "Dist");
    for (int i = 0; i < g->node_count; i++)
    {
        node *n = &g->nodes[i];
        printf("%6d | %5d | %5d\n", i, n->prev_node_id, n->dist);
    }
    printf("SOURCE | %5d | %5d\n", super_source.prev_node_id, super_source.dist);
    printf("  SINK | %5d | %5d\n", super_sink.prev_node_id, super_sink.dist);
}

/**
 * Find the next non-space token.
 */
bool find_next_token(char *data, int length, int *index)
{
    bool nextline = false;
    int i = *index;
    while (i < length && !isspace(data[i]))
    {
        i++;
    }
    while (i < length && isspace(data[i]))
    {
        if (data[i] == '\n')
        {
            nextline = true;
        }
        i++;
    }
    *index = i;

    return nextline;
}

/**
 * Maps a file to memory.
 */
char *mmap_file(const char *filename, int *length)
{
    int fd;
    if ((fd = open(filename, O_RDONLY)) == -1)
    {
        return MAP_FAILED;
    }
    int len = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char *data = (char *)mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    *length = len;
    return data;
}

/**
 * Parses the graphfile into a graph.
 */
graph *parse_graphfile(const char *graphfile)
{
    int i = 0;
    int length;
    char *data = mmap_file(graphfile, &length);
    if (data == MAP_FAILED)
    {
        return NULL;
    }

    // read header line
    int node_count = atoi(&data[i]);
    find_next_token(data, length, &i);
    int edge_count = atoi(&data[i]);
    find_next_token(data, length, &i);

    graph *g = malloc(sizeof(graph));
    g->total_node_count = node_count;
    g->node_count = node_count;
    g->edge_count = edge_count;
    g->super_source_node_id = g->total_node_count++;
    g->super_sink_node_id = g->total_node_count++;
    g->nodes = calloc(g->total_node_count, sizeof(node));

    while (i < length)
    {
        int node_id = atoi(&data[i]);
        if (find_next_token(data, length, &i))
            continue;

        int edge_dst_id = atoi(&data[i]);
        if (find_next_token(data, length, &i))
            continue;

        int edge_capacity = atoi(&data[i]);
        //printf("%d, %d, %d\n", node_id, edge_dst_id, edge_capacity);
        if (node_id < node_count && edge_dst_id < node_count)
        {
            node *n = &g->nodes[node_id];
            edge_add(&n->edges, edge_dst_id, edge_capacity);
        }
        if (find_next_token(data, length, &i))
            continue;
    }

    munmap(data, length);

    graph_populate_inverted_edges(g);
    graph_create_super_source(g);
    graph_create_super_sink(g);

    return g;
}

int main(int argc, const char *argv[])
{
    const char *graphfile = argc > 1 ? argv[1] : NULL;

    if (graphfile == NULL)
    {
        printf(
            "You must provide a graph file and optionally a name file.\n"
            "Usage: ./maxflow <graphfile>\n");
        return 1;
    }

    graph *graph = parse_graphfile(graphfile);
    if (graph == NULL)
    {
        perror("Failed to parse graph file");
        return 1;
    }

    bfs(graph);
    print_bfs_result(graph);

    printf("\nSource node: %d\nSink node: %d\n", graph->super_source_node_id, graph->super_sink_node_id);

    graph_free(graph);

    return 0;
}
