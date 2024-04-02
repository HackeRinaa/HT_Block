#ifndef HASH_FILE_H
#define HASH_FILE_H

#include <stdbool.h>

typedef enum HT_ErrorCode {
  HT_OK,
  HT_ERROR
} HT_ErrorCode;

#define MAX_OPEN_FILES 20
#define MAX_RECORDS 8 //  BF_BLOCK_SIZE / sizeof(Record)
#define MAX_BUCKETS 64

typedef struct Record {
	int id;
	char name[15];
	char surname[20];
	char city[20];
} Record;

typedef struct Index{ // πληροφορίες για το αρχείο
	int fileCount;
	int fileDesc[MAX_OPEN_FILES];
} Index;

typedef struct Bucket{
  int recordCount;
  int localDepth;
  Record records[MAX_RECORDS]; 
} Bucket;

typedef struct HashTable{
  int depth; 
  int buckets[MAX_BUCKETS];
  int nextHT; // δείκτης στον επόμενο πίνακα κατακερματισμού
} HashTable;



int hashFunction(int id, int depth); 




/* 
 * HT_Init χρησιμοπιείται για την αρχικοποίηση της δομής. 
 * Σε επιτυχία επιστρέφει HT_OK 
*/
HT_ErrorCode HT_Init();

/*
  * Η HT_CreateIndex συνάρτηση χρησιμοποιείται για να δημιουργήσει και να αρχικοποιήσει ενα άδειο αρχείο με το όνομα fileName.
  * Αν υπάρχει το αρχείο επιστρέφεται κωδικός λάθους, αλλίως επιστρέφει HT_OK. 
 */
HT_ErrorCode HT_CreateIndex(
	const char *fileName,
	int depth
);

/*
  * Αυτή η συνάρτηση ανοίγει ένα αρχείο με όνομα fileName.
  * Αν ανοίξει επιστρέφει HT_OK, αλλίως επιστρέφει μηνύμα λάθους.
 */
HT_ErrorCode HT_OpenIndex(
	const char *fileName, 	
  	int *indexDesc        	
);

/*
  * Αυτή η συνάρτηση κλείνει το αρχείο, το οποίο βρίσκεται στην θέση indexDesc.
  * Η συνάρτηση επιστρέφει HT_OK αν το αρχείο κλείσει με επιτυχία, αλλίως μήνυμα λάθους.
 */
HT_ErrorCode HT_CloseFile(
	int indexDesc 			
	);

/*
  * Η HT_InsertEntry συνάρτηση χρησιμοποιείται για να εισάγει μια εγγράφη στο αρχείο.
  * Αν εκτέλεστει επιτυχώς επιστρέφει HT_OK, αλλίως μήνυμα λάθους.
 */
HT_ErrorCode HT_InsertEntry(
	int indexDesc,		
	Record record		
	);

/*
  * Η HT_PrintAllEntries συνάρτηση χρησιμοποείται για την εκτύπωση όλων των εγγραφών για τισ οποίες το record.id έχει τιμή.
  * Αν το id είναι NULL τότε θα εκτυπωθούν όλες οι εγγραφές του αρχείου κατακερματισμού.
  * Αν εκτελεστεί σωστά επιστρέφει HΤ_OK, αλλιίως μηνύμα λάθους.
 */
HT_ErrorCode HT_PrintAllEntries(
	int indexDesc,			
	int *id 				
	);


#endif // HASH_FILE_H