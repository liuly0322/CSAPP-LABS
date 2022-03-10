#ifndef C_SIM_LRU
#define C_SIM_LRU

#include <string.h>

struct node {
    int key;
    struct node* prev;
    struct node* next;
};

struct hash {
    struct node* unused;
    struct hash* next;
};

typedef struct {
    int size;
    int capacity;
    struct hash* table;

    struct node* head;
    struct node* tail;
} LRUCache;

struct hash* HashMap(struct hash* table, int key, int capacity) {
    int addr = key % capacity;
    return &table[addr];
}

void HeadInsertion(struct node* head, struct node* cur) {
    if (cur->prev == NULL && cur->next == NULL) {
        cur->prev = head;
        cur->next = head->next;
        head->next->prev = cur;
        head->next = cur;
    } else {
        struct node* fisrt = head->next;
        if (fisrt != cur) {
            cur->prev->next = cur->next;
            cur->next->prev = cur->prev;
            cur->next = fisrt;
            cur->prev = head;
            head->next = cur;
            fisrt->prev = cur;
        }
    }
}

LRUCache* lRUCacheCreate(int capacity) {
    LRUCache* obj = (LRUCache*)malloc(sizeof(LRUCache));
    obj->table = (struct hash*)malloc(capacity * sizeof(struct hash));
    memset(obj->table, 0, capacity * sizeof(struct hash));
    obj->head = (struct node*)malloc(sizeof(struct node));
    obj->tail = (struct node*)malloc(sizeof(struct node));

    obj->head->prev = NULL;
    obj->head->next = obj->tail;
    obj->tail->prev = obj->head;
    obj->tail->next = NULL;

    obj->size = 0;
    obj->capacity = capacity;
    return obj;
}

int lRUCacheGet(LRUCache* obj, int key) {
    struct hash* addr = HashMap(obj->table, key, obj->capacity);
    addr = addr->next;
    if (addr == NULL) {
        return 0;
    }
    while (addr->next != NULL && addr->unused->key != key) {
        addr = addr->next;
    }
    if (addr->unused->key == key) {
        HeadInsertion(obj->head, addr->unused);
        return 1;
    }
    return 0;
}

int lRUCachePut(LRUCache* obj, int key) {
    struct hash* addr = HashMap(obj->table, key, obj->capacity);
    if (obj->size >= obj->capacity) {
        struct node* last = obj->tail->prev;
        struct hash* remove = HashMap(obj->table, last->key, obj->capacity);
        struct hash* ptr = remove;
        remove = remove->next;
        while (remove->unused->key != last->key) {
            ptr = remove;
            remove = remove->next;
        }
        ptr->next = remove->next;
        remove->next = NULL;
        remove->unused = NULL;
        free(remove);
        struct hash* new_node = (struct hash*)malloc(sizeof(struct hash));
        new_node->next = addr->next;
        addr->next = new_node;
        new_node->unused = last;
        last->key = key;
        HeadInsertion(obj->head, last);
        return 0;
    } else {
        struct hash* new_node = (struct hash*)malloc(sizeof(struct hash));
        new_node->unused = (struct node*)malloc(sizeof(struct node));
        new_node->next = addr->next;
        addr->next = new_node;
        new_node->unused->prev = NULL;
        new_node->unused->next = NULL;
        new_node->unused->key = key;
        HeadInsertion(obj->head, new_node->unused);
        ++(obj->size);
        return 1;
    }
}

#endif