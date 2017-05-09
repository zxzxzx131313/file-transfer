#include <stdio.h>
#include <stdlib.h>

#define BLOCK_SIZE 8

int hash(char *hash_val, FILE *f) {
    char ch;
    int hash_index = 0;

    for (int index = 0; index < BLOCK_SIZE; index++) {
        hash_val[index] = '\0';
    }

    int num_read = 0;
    while((num_read = fread(&ch, 1, 1, f)) != 0) {
        hash_val[hash_index] ^= ch;
        hash_index = (hash_index + 1) % BLOCK_SIZE;
	if(num_read != 1){
		fprintf(stderr, "Hash: hash reads file error\n");
		return 1;
	}
    }
    return 0;
 }


int check_hash(const char *hash1, const char *hash2) {
    for (long i = 0; i < BLOCK_SIZE; i++) {
        if (hash1[i] != hash2[i]) {
            printf("Index %ld: %c\n", i, hash1[i]);
            return 1;
        }
    }
    return 0;
}
