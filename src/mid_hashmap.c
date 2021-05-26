// HashMap.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include "mid_hashmap.h"
void defaultPut(HashMap* hashMap, void* key, void* value);
int defaultHashCode(HashMap* hashMap, void* key);
int murmur3_32(HashMap* hashMap, void * key);
int defaultEqual(void* key1, void* key2);
void defaultPut(HashMap* hashMap, void* key, void* value) ;
void * defaultGet(HashMap * hashMap, void * key) ;
int defaultExists(HashMap * hashMap, void * key) ;
void* defaultRemove(HashMap* hashMap, void* key) ;
void defaultClear(HashMap *hashMap);
HashMap* createHashMap(HashCode hashCode, Equal equal , int _listSize) ;
void resetHashMap(HashMap* hashMap, int new_listSize);

int defaultHashCode(HashMap* hashMap, void* key) {
	char* k = (char*)key;
	unsigned long h = 0;
	while (*k) {
		h = (h << 4) + *k++;
		unsigned long g = h & 0xF0000000L;
		if (g) {
			h ^= g >> 24;
		}
		h &= ~g;
	}
	return h % hashMap->listSize;
}

int murmur3_32(HashMap* hashMap, void * kkey)
{
	int len = hashMap->listSize;
	int seed = 6;
	const char * key = (const char *)kkey;

	static const int c1 = 0xcc9e2d51;
	static const int c2 = 0x1b873593;
	static const int r1 = 15;
	static const int r2 = 13;
	static const int m = 5;
	static const int n = 0xe6546b64;
 
	int hash = seed;
 
	const int nblocks = len / 4;
	const int *blocks = (const int *)key;
	int i;
	for (i = 0; i < nblocks; i++)
	{
		int k = blocks[i];
		k *= c1;
		k = (k << r1) | (k >> (32 - r1));
		k *= c2;
 
 
		hash ^= k;
		hash = ((hash << r2) | (hash >> (32 - r2))) * m + n;
	}
 
	const int *tail = (const int *)(key + nblocks * 4);
	int k1 = 0;
 
	switch (len & 3)
	{
	case 3:
		k1 ^= tail[2] << 16;
	case 2:
		k1 ^= tail[1] << 8;
	case 1:
		k1 ^= tail[0];
 
 
		k1 *= c1;
		k1 = (k1 << r1) | (k1 >> (32 - r1));
		k1 *= c2;
		hash ^= k1;
	}
 
	hash ^= len;
	hash ^= (hash >> 16);
	hash *= 0x85ebca6b;
	hash ^= (hash >> 13);
	hash *= 0xc2b2ae35;
	hash ^= (hash >> 16);
 
	return hash;
}

int defaultEqual(void* key1, void* key2) {
	return strcmp((char *)key1, (char *)key2) ? 0 : 1;
}

void defaultPut(HashMap* hashMap, void* key, void* value) {
	
	int index = hashMap->hashCode(hashMap, key);
	if (hashMap->hash_list[index].key == NULL) {
		hashMap->size++;
		// 该地址为空时直接存储
		hashMap->hash_list[index].key = key;
		hashMap->hash_list[index].value = value;
		// printf("put key %s, value %s\n", (char*)key, (char*)value);
	}
	else {

		Entry* current = & hashMap->hash_list[index];
		while (current != NULL) {
			if (hashMap->equal(key, current->key)) {
				// 对于键值已经存在的直接覆盖
				current->value = value;
				// printf("put key %s, value %s\n", (char*)key, (char*)value);
				return;
			}
			current = current->next;
		};

		// 发生冲突则创建节点挂到相应位置的next上
		Entry* entry = (Entry*) malloc (sizeof(Entry));
		entry->key = key;
		entry->value = value;
		entry->next = hashMap->hash_list[index].next;
		hashMap->hash_list[index].next = entry;
		hashMap->size++;
		// printf("put key %s, value %s\n", (char*)key, (char*)value);
	}



	if (hashMap->size >= hashMap->listSize && hashMap->changeSize) {

		// 内存扩充至原来的两倍
		// *注: 扩充时考虑的是当前存储元素数量与存储空间的大小关系，而不是存储空间是否已经存满，
		// 例如: 存储空间为10，存入了10个键值对，但是全部冲突了，所以存储空间空着9个，其余的全部挂在一个上面，
		// 这样检索的时候和遍历查询没有什么区别了，可以简单这样理解，当我存入第11个键值对的时候一定会发生冲突，
		// 这是由哈希函数本身的特性(取模)决定的，冲突就会导致检索变慢，所以这时候扩充存储空间，对原有键值对进行
		// 再次散列，会把冲突的数据再次分散开，加快索引定位速度。
		resetHashMap(hashMap, hashMap->listSize * 2);
		// printf("hashMap->listSize resize to %d\n", hashMap->listSize);
	}
}

void * defaultGet(HashMap * hashMap, void * key) {
	if (hashMap->exists(hashMap, key)) {
		int index = hashMap->hashCode(hashMap, key);
		Entry*entry = &hashMap->hash_list[index];
		while (entry != NULL) {
			if (hashMap->equal(entry->key, key)) {
				return entry->value;
			}
			entry = entry->next;
		}
	}
	return NULL;
}

int defaultExists(HashMap * hashMap, void * key) {
	int index = hashMap->hashCode(hashMap, key);
	Entry *entry = &hashMap->hash_list[index];
	if (entry->key == NULL) {
		return 0;
	}
	else {
		while (entry != NULL) {
			if (hashMap->equal(entry->key, key)) {
				return 1;
			}
			entry = entry->next;
		}
		return 0;
	}
}

void* defaultRemove(HashMap* hashMap, void* key) {
	int index = hashMap->hashCode(hashMap, key);
	Entry* entry = &hashMap->hash_list[index];
	if (entry->key == NULL) {	// 目前这个key不存任何value
		return NULL;
	}
	void* entryKey = entry->key;
	int result = 0;
	if (hashMap->equal(entry->key, key)) {
		hashMap->size--;
		if (entry->next != NULL) {
			Entry* temp = entry->next;
			entry->key = temp->key;
			entry->value = temp->value;
			entry->next = temp->next;
			free(temp);
		}
		else {
			entry->key = NULL;
			entry->value = NULL;
		}
		result = 1;
	}
	else {
		Entry* p = entry;
		entry = entry->next;
		while (entry != NULL) {
			if (hashMap->equal(entry->key, key)) {
				hashMap->size--;
				p->next = entry->next;
				free(entry);
				result = 1;
				break;
			}
			p = entry;
			entry = entry->next;
		};
	}
	// 如果空间占用不足一半，则释放多余内存
	if (result && hashMap->size < hashMap->listSize / 2 && hashMap->changeSize) {
		resetHashMap(hashMap, hashMap->listSize / 2);
	}
	return entryKey;
}


void defaultClear(HashMap *hashMap) {
	int i;
	for ( i = 0; i < hashMap->listSize; i++) {
		// 释放冲突值内存
		Entry *entry = hashMap->hash_list[i].next;
		while (entry != NULL) {
			Entry* next = entry->next; 
			free(entry);
			entry = next;
		}
		hashMap->hash_list[i].next = NULL;
	}
	// 释放存储空间
	free(hashMap->hash_list);
	hashMap->hash_list = NULL;
	hashMap->size = -1;
	hashMap->listSize = 0;
}

HashMap* createHashMap(HashCode hashCode, Equal equal, int _listSize) {
	HashMap *hashMap = (HashMap *) malloc (sizeof(HashMap));
	if (hashMap == NULL) {
		return NULL;
	}
	hashMap->size = 0;
	hashMap->listSize = _listSize == -1 ? DEFAULT_MAP_SIZE : _listSize;
	hashMap->hashCode = hashCode == NULL ? defaultHashCode : hashCode;
	hashMap->equal = equal == NULL ? defaultEqual : equal;
	hashMap->exists = defaultExists;
	hashMap->get = defaultGet;
	hashMap->put = defaultPut;
	hashMap->remove = defaultRemove;
	hashMap->clear = defaultClear;
	hashMap->changeSize = 1;

	// 起始分配8个内存空间，溢出时会自动扩充
	hashMap->hash_list = (Entry *)malloc(sizeof(Entry) * hashMap->listSize);
	if (hashMap->hash_list == NULL) {
		return NULL;
	}
	Entry* p = hashMap->hash_list;
	int i;
	for (i = 0; i < hashMap->listSize; i++) {
		p[i].key = p[i].value = p[i].next = NULL;
	}
	return hashMap;
}


void resetHashMap(HashMap* hashMap, int new_listSize) {

	if (new_listSize < DEFAULT_MAP_SIZE) return;
	// printf("hashMap->size is %d\n", hashMap->size);

	// 将键值对转移到临时存储空间，同时对键值对重新赋值
	Entry * tempList = (Entry *) malloc( sizeof(struct entry) * hashMap->size);
	int i = 0, count = 0;
	for(; i < hashMap->listSize; i++){
		Entry* cnt = &hashMap->hash_list[i];
		int j = 0;
		while(cnt){
			if(cnt->key == NULL){
				// printf("%d is miss\n",i );
				break;
			}
			tempList[count].key = cnt->key;
			tempList[count].value = cnt->value;
			tempList[count].next = NULL;
			// printf("Reput key %s, value %s\n", tempList[count].key, tempList[count].value);
			Entry * cnt_tp = cnt;
			cnt = cnt->next;
			if(j !=0)
				free(cnt_tp);
			count++;
			j++;
		}
	}
	// printf("loop 1 down! count = %d\n", count);

	// 更改内存大小
	hashMap->listSize = new_listSize;
	hashMap->hash_list  = (Entry*)realloc(hashMap->hash_list, hashMap->listSize * sizeof(struct entry));

	// printf("更改内存大小ok!\n");

	// 初始化数据
	for ( i = 0; i < hashMap->listSize; i++) {
		hashMap->hash_list[i].key = NULL;
		hashMap->hash_list[i].value = NULL;
		hashMap->hash_list[i].next = NULL;
	}

	// printf("初始化数据ok! , hashMap->size = %d \n", hashMap->size);

	// 将所有键值对重新写入内存
	int old_length = hashMap->size;
	hashMap->size = 0;
	for (i = 0; i <old_length; i++) {
		hashMap->put(hashMap, tempList[i].key, tempList[i].value);
		// printf("%d\n", i);
	}

	// printf("resie ok! i== %d\n", i);
	free(tempList);
	
}


/*
int main()
{
	HashMap * hashmap =  createHashMap(defaultHashCode, NULL);
	int test_num = 1000;
	int i =0;
	char ** file_names = (char **) malloc (test_num * sizeof(char*));
	for(i =0; i< test_num; i++){
		// const char * temp1 = "/temp/gtwang/Spark/.swap.wsap";
		char * temp2 = malloc(4);
		sprintf(temp2, "%d", i);
		// file_names[i] =strcat(temp1, temp2);
		file_names[i] =temp2;
	}	
	// for(i=0; i< test_num; i++){
	// 	printf("%s\n", file_names[i]);
	// }
	for(i =0; i< test_num; i++){
		hashmap->put(hashmap, file_names[i], file_names[i]);
	}
	for(i =0; i< test_num; i++){
		if(file_names[i] != hashmap->get(hashmap, file_names[i]) ){
			printf("ERRPR\n");
		}
	}
	// Entry * cnt = hashmap->hash_list;
	// i = 0;
	// int count = 0;
	// for(; i < hashmap->listSize; i++){
	// 	while(cnt){
	// 		printf("%s, %s", cnt->key,  cnt->value);
	// 		cnt = cnt->next;
	// 		count++;
	// 	}
	// 	cnt = (hashmap->hash_list + i);
	// }

	return 0;
}
*/