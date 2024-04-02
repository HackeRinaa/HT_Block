#include "hash_file.h"
#include "bf.h"
#include <math.h> 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CALL_BF(call)             \
    {                             \
        BF_ErrorCode code = call; \
        if (code != BF_OK) {      \
            BF_PrintError(code);  \
            return HT_ERROR;      \
        }                         \
    }

static Index indexTable; // index table for the open files



//Η συνάρτηση για την εισαγωγή στο πίνακα καταρκεματισμόυ 
int hashFunction(int id, int depth){
  int index =id;
  int reverseIndex =0;
  int allBits=32;
  while (allBits--){        
    
    //Αντιστροφή των bits
    reverseIndex = (reverseIndex<<1) | (index & 1);
    index>>=1;
  
  }

  // get the last depth bits
  reverseIndex = reverseIndex >>(32-depth); 
  int reverseIndex2 =0;
  while (depth--){ 
    
    // to be read from the end for the buddy system to work
    reverseIndex2 = (reverseIndex2<<1) | (reverseIndex & 1);
    reverseIndex>>=1;
  }
  return reverseIndex2;
}

HT_ErrorCode HT_Init()
{
    CALL_BF(BF_Init(LRU));
    indexTable.fileCount = 0;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        indexTable.fileDesc[i] = -1;
    }
    return HT_OK;
}

HT_ErrorCode HT_CreateIndex(const char* fileName, int depth)
{

    if (indexTable.fileCount == MAX_OPEN_FILES) {
      return HT_ERROR; // εαν εχουμε φτάσει τον μέγιστο αριθμό ανοικτών αρχείων 
    }
    int fd1;
    BF_Block* hashBlock;
    BF_Block_Init(&hashBlock);
    CALL_BF(BF_CreateFile(fileName));
    CALL_BF(BF_OpenFile(fileName, &fd1));

    // το πρώτο μπλόκ του πίνακα θα κρατάει σχετικές με τον πίνακα πληροφορίες ενω τα υπόλοιπα θα είναι buckets
    char* data;
    CALL_BF(BF_AllocateBlock(fd1, hashBlock));
    data = BF_Block_GetData(hashBlock);
    HashTable hashTable;
    hashTable.depth = depth;
    hashTable.nextHT = -1; // το τέλος ή  το άδειο το συμβολίζουμε με -1
    for (int i = 0; i < 64; i++) {
        hashTable.buckets[i] = -1; 
    }
    memcpy(data, &hashTable, sizeof(HashTable));

    BF_Block_SetDirty(hashBlock);
    CALL_BF(BF_UnpinBlock(hashBlock));
    CALL_BF(BF_CloseFile(fd1));

    return HT_OK;
}

HT_ErrorCode HT_OpenIndex(const char* fileName, int* indexDesc)
{
    if (indexTable.fileCount == MAX_OPEN_FILES)
        return HT_ERROR;
    int fd;
    CALL_BF(BF_OpenFile(fileName, &fd));
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        // εισαγωγή τις πληροφορίας(αρχέιων) στον πίνακα 
        if (indexTable.fileDesc[i] == -1) {
            indexTable.fileDesc[i] = fd;
            indexTable.fileCount += 1;
            *indexDesc = i;
            return HT_OK;
        }
    }
    return HT_ERROR;
}

HT_ErrorCode HT_CloseFile(int indexDesc){

    if ((indexDesc < MAX_OPEN_FILES) && (indexDesc > -1) && (indexTable.fileDesc[indexDesc] != -1)) {
        CALL_BF(BF_CloseFile(indexTable.fileDesc[indexDesc])); 
        indexTable.fileDesc[indexDesc] = -1; 
        indexTable.fileCount -= 1;
        return HT_OK;
    }
    return HT_ERROR;
}

HT_ErrorCode HT_InsertEntry(int indexDesc, Record record) {
  int fileDesc;
  int holdthis = indexDesc;
  
  if ((indexDesc < MAX_OPEN_FILES) && (indexDesc > -1) && (indexTable.fileDesc[indexDesc] != -1))
  {
    fileDesc = indexTable.fileDesc[indexDesc];
  }
  else
  {
    return HT_ERROR;
  }
  
  char *hashTable;
  BF_Block *hashBlock;
  BF_Block_Init(&hashBlock);
  //Το πρώτο μπλόκ περιέχει τις πληροφορίες του πίνακα 
  CALL_BF(BF_GetBlock(fileDesc, 0, hashBlock)); 
  hashTable = BF_Block_GetData(hashBlock);

  //Χρήση της συνάρτησης καταρκεματισμόυ για την εύρεση της θέσης του αρχείου με βάση το id 
  int Position = hashFunction(record.id, ((HashTable *)hashTable)->depth);
  
  int HashPosition = Position / 64; // Έχουμε επιλέξει οτι έχουμε max 64 buckets
  for (int i = 0; i < HashPosition; i++)
  {
    int nextPosition = ((HashTable *)hashTable)->nextHT;
    CALL_BF(BF_UnpinBlock(hashBlock));
    CALL_BF(BF_GetBlock(fileDesc, nextPosition, hashBlock));
    hashTable = BF_Block_GetData(hashBlock);
  }
  int bucketDesc = ((HashTable *)hashTable)->buckets[Position % 64];


  // Περίπτωση 1:  Χρειάζεται να δημιουργήσουμε ένα νέο bucket
  if (bucketDesc == -1){ 
    BF_Block *litoBucket;
    BF_Block_Init(&litoBucket);
    char *data;
    CALL_BF(BF_AllocateBlock(fileDesc, litoBucket));
    data = BF_Block_GetData(litoBucket);
    Bucket bucketL;
    bucketL.records[0] = record;
    bucketL.recordCount = 1;
    // Για αρχή ένα μόνο μπλόξ του πίνακα θα δείχνει στο bucket αυτό
    bucketL.localDepth = ((HashTable *)hashTable)->depth; 
    memcpy(data, &bucketL, sizeof(Bucket));

    BF_Block_SetDirty(litoBucket);
    CALL_BF(BF_UnpinBlock(litoBucket));

    int newBlockCounter;
    CALL_BF(BF_GetBlockCounter(fileDesc, &newBlockCounter));
    ((HashTable *)hashTable)->buckets[Position % 64] = newBlockCounter - 1;

    
    BF_Block_SetDirty(hashBlock);      // Εφόσον έχουμε κάνει αλλαγές
    CALL_BF(BF_UnpinBlock(hashBlock)); // Κάνουμε unpin του μπλοκ αυτού

    return HT_OK;
  }
  // Περίπτωση 2: Το ζητούμενο bucket υπάρχει ήδη 
  else
  { 
    
    char *bucketData;
    BF_Block *bucketBlock;
    BF_Block_Init(&bucketBlock);
    CALL_BF(BF_GetBlock(fileDesc, bucketDesc, bucketBlock));
    bucketData = BF_Block_GetData(bucketBlock);
    // 1η υπο-περίπτωση : Το βάθος του πίνακα είναι μεγαλύτερο απο το τοπικό του bucket
    if (((Bucket *)bucketData)->recordCount == 8){
      if (((HashTable *)hashTable)->depth > ((Bucket *)bucketData)->localDepth)
      { // Στρατιγική : bucket splitting 
        
        // Ελέγχω αν έχουν την ίδια τιμή hash 
        int count =0;
        for (int i = 0; i < ((Bucket *)bucketData)->recordCount; i++){
          Record r = ((Bucket *)bucketData)->records[i];
          int whereNew = hashFunction(r.id, ((HashTable *)hashTable)->depth);
          if (whereNew == Position) count++;
        }
        if (count==8){
          
          // Αν το bucket είναι γεμάτο πρέπει να προχωρήσουμε σε resize του πίνακα 
          ((Bucket *)bucketData)->localDepth = ((HashTable *)hashTable)->depth;
          
          // Οι δείκτες του πίνακα πρέπει να αλλαχτούν ώστε να ειναι up-to-date
          int i = (int)pow(2.0, (double)(((HashTable *)hashTable)->depth));
          if (i > 64)
          {
            i = 64;
          }
          char* hashT;
          BF_Block *helpingBlock;
          BF_Block_Init(&helpingBlock);
          int HTindex=0;
          do{
            CALL_BF(BF_GetBlock(fileDesc, HTindex, helpingBlock));
            hashT = BF_Block_GetData(helpingBlock);
            for (int jj=0;jj<i;jj++){
              if (((HashTable *)hashT)->buckets[jj]== bucketDesc&& jj!=Position){
                //  Αφαιρώ όλους τους προηγούμενος δείκτες αφου πλέον δεν εξυπηρετούν τον νεό πίνακα που θα υποστεί resize
                ((HashTable *)hashT)->buckets[jj]=-1;
              }
            }
            HTindex = ((HashTable *)hashT)->nextHT;
            BF_Block_SetDirty(helpingBlock);
            CALL_BF(BF_UnpinBlock(helpingBlock));
            if (HTindex!=-1) BF_UnpinBlock(helpingBlock);
          }while(HTindex!=-1);
          BF_Block_SetDirty(bucketBlock);
          BF_UnpinBlock(bucketBlock);
          BF_Block_SetDirty(hashBlock);
          BF_UnpinBlock(hashBlock);

          // Καλώ αναδρομικά την συνάρτηση HT_InsertEntry ώστε να πραγματοποιηθεί ο διπλασιασμός του μεγέθους του πίνακα 
          // H μεταβλητή holdthis κρατάει αποθηκευμένο το τρέχον indexDesc
          return HT_InsertEntry(holdthis, record);
        }
        // Τώρα που έχει πραγματοποιηθεί ο διπλασιασμός του μεγέθους του πίνακα:
        BF_Block *littleBucket;
        BF_Block_Init(&littleBucket);
        char *data;
        CALL_BF(BF_AllocateBlock(fileDesc, littleBucket));
        data = BF_Block_GetData(littleBucket);
        Bucket bucketL;
        Bucket tempBucket;
        // ΒΗΜΑ 1: Άρχικοποιώ τα νέα bucket
        int bucketL_records = 0;
        int bucketdata_records = 0;
        int oldBucketPosition = ((HashTable *)hashTable)->buckets[Position % 64];
        int originalPositions[8];
        for (int i = 0; i < ((Bucket *)bucketData)->recordCount; i++)
        {
          Record r = ((Bucket *)bucketData)->records[i];
          int whereNew = hashFunction(r.id, ((HashTable *)hashTable)->depth);
          originalPositions[i]=r.id; // κρατάμε αυτη την πληροφορία για μελλοντικά update 
          if (whereNew == Position)
          { // Εάν δεν έχουμε αλλαγή θέσης χρησιμοποιούμε ένα παλίο block 
            tempBucket.records[bucketdata_records] = r;
            tempBucket.recordCount = ++bucketdata_records;
          }
          else
          { // Αλλίως νέο  block
            bucketL.records[bucketL_records] = r;
            bucketL.recordCount = ++bucketL_records;
          }
        }

        // ΒΗΜΑ 2 : Εισαγωή του νέου αρχείου
        int whereNew = hashFunction(record.id, ((HashTable *)hashTable)->depth);
        int indexForSecondaryUpdate = 0;
        int newtooldrec =1;
        if (whereNew == Position)
        { // Δεν έχουμε αλλαγή block 
          tempBucket.records[bucketdata_records] = record;
          indexForSecondaryUpdate=bucketdata_records;
          tempBucket.recordCount = ++bucketdata_records;
        }
        else
        { // Νέο block
          newtooldrec = 0;
          bucketL.records[bucketL_records] = record;
          indexForSecondaryUpdate=bucketL_records;
          bucketL.recordCount = ++bucketL_records;
        }

        // Το Local depth θα καταχωρηθεί στο τέλος της διαδικασίας 
        tempBucket.localDepth =  0; 
        bucketL.localDepth = 0;
        memcpy(bucketData, &tempBucket, sizeof(Bucket));
        memcpy(data, &bucketL, sizeof(Bucket));

        int newBlockCounter;
        CALL_BF(BF_GetBlockCounter(fileDesc, &newBlockCounter));
        int newBucketPosition = newBlockCounter - 1;


        // ΒΗΜΑ 3 : Update για τους δείκτες σύμφωνα με τα καινούργια δεδομένα 
        // Η μεταβλητή αυτή χρησιμοποιείται για να αναδείξει πόσοι δείκτες δείχθουν σε αυτό το bucket 
        int K = 0;  
        int positions[8];
        for (int j = 0; j < 8; j++){
          positions[j] = -1;
        }
        for (int j = 0; j < ((Bucket *)bucketData)->recordCount; j++){
          char *c;
          BF_Block *hashBlockT;
          BF_Block_Init(&hashBlockT);
          int p = hashFunction(((Bucket *)bucketData)->records[j].id, ((HashTable *)hashTable)->depth);
          int unique = 1;
          for (int i = 0; i < 8; i++)
          {
            if (positions[i] == p)
              unique = 0;
          }
          if (unique == 1)
          {
            positions[j] = p;
            K++;
          }
          CALL_BF(BF_GetBlock(fileDesc, 0, hashBlockT)); 
          c = BF_Block_GetData(hashBlockT);
          for (int i = 0; i < p / 64; i++)
          {
            int nextPosition = ((HashTable *)c)->nextHT;
            CALL_BF(BF_UnpinBlock(hashBlock));
            CALL_BF(BF_GetBlock(fileDesc, nextPosition, hashBlockT));
            c = BF_Block_GetData(hashBlockT);
          }
          ((HashTable *)c)->buckets[p % 64] = oldBucketPosition;
          
        }
        ((Bucket *)bucketData)->localDepth = ((HashTable *)hashTable)->depth - (int)log2(K);

        // Για νέο bucket 
        K = 0;
        for (int j = 0; j < 8; j++)
        {
          positions[j] = -1;
        }
        for (int j = 0; j < ((Bucket *)data)->recordCount; j++)
        {
          char *c;
          BF_Block *hashBlockT;
          BF_Block_Init(&hashBlockT);
          int p = hashFunction(((Bucket *)data)->records[j].id, ((HashTable *)hashTable)->depth);
          int unique = 1;
          for (int i = 0; i < 8; i++)
          {
            if (positions[i] == p)
              unique = 0;
          }
          if (unique == 1)
          {
            positions[j] = p;
            K++;
          }
          CALL_BF(BF_GetBlock(fileDesc, 0, hashBlockT)); 
          c = BF_Block_GetData(hashBlockT);
          for (int i = 0; i < p / 64; i++)
          {
            int nextPosition = ((HashTable *)c)->nextHT;
            CALL_BF(BF_UnpinBlock(hashBlock));
            CALL_BF(BF_GetBlock(fileDesc, nextPosition, hashBlockT)); 
            c = BF_Block_GetData(hashBlockT);
          }
          ((HashTable *)c)->buckets[p % 64] = newBucketPosition;
          

          
          BF_Block_SetDirty(hashBlockT);
          CALL_BF(BF_UnpinBlock(hashBlockT));
        }                                    
        ((Bucket *)data)->localDepth = ((HashTable *)hashTable)->depth - (int)log2(K);
        BF_Block_SetDirty(littleBucket);
        CALL_BF(BF_UnpinBlock(littleBucket));
        BF_Block_SetDirty(bucketBlock);
        CALL_BF(BF_UnpinBlock(bucketBlock));
        BF_Block_SetDirty(hashBlock);
        CALL_BF(BF_UnpinBlock(hashBlock));
        return HT_OK;
      }
      //2η υπο-περίπτωση : Το βάθος του πίνακα είναι ίσο με το τοπικό του bucket
      else if (((HashTable *)hashTable)->depth == ((Bucket *)bucketData)->localDepth)
      { // Διπλασιασμός του μεγέθους 
        int new_depth = ((HashTable *)hashTable)->depth + 1;
        int newBlockCounter;
        if (new_depth > 6) { // 6 γιατί  2^6=64 που είναι ο μέγιστος αριθμός των bucket
          int howManyChanges = (int)pow(2.0, (double)(new_depth - 7));
          char *hashToLink;
          BF_Block *helpingBlock;
          BF_Block_Init(&helpingBlock);
          int HTindex=0;
          do{
            CALL_BF(BF_GetBlock(fileDesc, HTindex, helpingBlock));
            hashToLink = BF_Block_GetData(helpingBlock);
            HTindex = ((HashTable *)hashToLink)->nextHT;
            if (HTindex!=-1) BF_UnpinBlock(helpingBlock);
          }while(HTindex!=-1);
          for (int i = 0; i < howManyChanges; i++){ // Δημιουργία νέου μπλόκ αν χρειαστεί 
            BF_Block *newHashBlock;
            BF_Block_Init(&newHashBlock);
            char *newData;
            CALL_BF(BF_AllocateBlock(fileDesc, newHashBlock));
            newData = BF_Block_GetData(newHashBlock);
            HashTable newHashTable;
            newHashTable.depth = ((HashTable *)hashToLink)->depth; 
            newHashTable.nextHT = -1;                               
            for (int i = 0; i < 64; i++)
            {
              newHashTable.buckets[i] = -1; 
            }
            memcpy(newData, &newHashTable, sizeof(HashTable));
            BF_Block_SetDirty(newHashBlock);
            CALL_BF(BF_UnpinBlock(newHashBlock));
            CALL_BF(BF_GetBlockCounter(fileDesc, &newBlockCounter));
            ((HashTable *)hashToLink)->nextHT = newBlockCounter - 1;
            BF_Block_SetDirty(helpingBlock);
            CALL_BF(BF_UnpinBlock(helpingBlock));
            HTindex = newBlockCounter - 1;
            CALL_BF(BF_GetBlock(fileDesc, HTindex, helpingBlock));
            hashToLink = BF_Block_GetData(helpingBlock);
          }
          BF_Block_SetDirty(helpingBlock);
          CALL_BF(BF_UnpinBlock(helpingBlock));
        }

        // Για τους δείκτες (φιλλαράκια)
        char *hashTableChanges;
        char *temporaryHT;
        int newSize = (int)pow(2.0, (double)new_depth);
        int HTindex = 0;
        int hashTableind = 0;
        BF_Block *hashBlock2;
        BF_Block_Init(&hashBlock2);
        BF_Block *helpingHashBlock;
        BF_Block_Init(&helpingHashBlock);

        do
        {
          CALL_BF(BF_GetBlock(fileDesc, HTindex, hashBlock2)); // first block
          hashTableChanges = BF_Block_GetData(hashBlock2);
          ((HashTable *)hashTableChanges)->depth = new_depth;
          int i = newSize > 64 ? 64 : newSize;
        
          for (int index = 0; index < i; index++){
            // Οι παλιοί δείκτες του πίνακα δέιχνουν στα καινούργια με βοήθεια απο τα φιλαράκια
            int buddyIndex = index + newSize;
            int HTbuddy = buddyIndex/64;
            
            int tempindex = 0;
            CALL_BF(BF_GetBlock(fileDesc, tempindex , helpingHashBlock));
            temporaryHT =  BF_Block_GetData(helpingHashBlock);
            tempindex  = ((HashTable *)temporaryHT)->nextHT;
            
            for (int g =0; g<HTbuddy-1; g++){
              CALL_BF(BF_UnpinBlock(helpingHashBlock));
              CALL_BF(BF_GetBlock(fileDesc, tempindex , helpingHashBlock));
              temporaryHT =  BF_Block_GetData(helpingHashBlock);
              tempindex  = ((HashTable *)temporaryHT)->nextHT;
            }
            // φιλαράκια , που είναι πολλαπλάσιο του 2, δείχνουν στο ίδιο bucket 
            ((HashTable *)temporaryHT)->buckets[buddyIndex%64] = ((HashTable *)hashTableChanges)->buckets[index];
            BF_Block_SetDirty(helpingHashBlock);
            CALL_BF(BF_UnpinBlock(helpingHashBlock));

          }
          HTindex = ((HashTable *)hashTableChanges)->nextHT;
          hashTableind+=1;
          BF_Block_SetDirty(hashBlock2);
          CALL_BF(BF_UnpinBlock(hashBlock2));

        } while (HTindex != -1);
        // Πραγματοποιούμε αναδρομικά την ίδια διαδικασία για όλα τα νέα bucket 
        BF_Block_SetDirty(hashBlock2);
        BF_UnpinBlock(hashBlock2);
        BF_Block_SetDirty(helpingHashBlock);
        BF_UnpinBlock(helpingHashBlock);
        BF_Block_SetDirty(bucketBlock);
        BF_UnpinBlock(bucketBlock);
        BF_Block_SetDirty(hashBlock);
        BF_UnpinBlock(hashBlock);
        return HT_InsertEntry(holdthis, record); 
      }
      else
      {
        return HT_ERROR; // Αν προκύψει πρόβλημα με τα τοπικά βάθη 
      }
    }
    // Περέπτωση 2η : Αν το bucket έχει χώρο , τότε απλά το τοποθετώ εκεί
    else
    { 
      int newIndexofRec = ((Bucket *)bucketData)->recordCount;
      ((Bucket *)bucketData)->records[newIndexofRec] = record;
      ((Bucket *)bucketData)->recordCount += 1;


      BF_Block_SetDirty(bucketBlock);
      CALL_BF(BF_UnpinBlock(bucketBlock));
      BF_Block_SetDirty(hashBlock);
      CALL_BF(BF_UnpinBlock(hashBlock));
      return HT_OK;
    }
  }
  // Για όποιο άλλο λόγο αποτύχει η εισαγωγλη επιστρέφω HT_ERROR
  return HT_ERROR;
}


HT_ErrorCode HT_PrintAllEntries(int indexDesc, int* id) {
    int fileDesc;
    if ((indexDesc < MAX_OPEN_FILES) && (indexDesc > -1) && (indexTable.fileDesc[indexDesc] != -1)) {
        fileDesc = indexTable.fileDesc[indexDesc];
    } else {
        return HT_ERROR;
    }

    int num_of_blocks;
    CALL_BF(BF_GetBlockCounter(fileDesc, &num_of_blocks));
    if (num_of_blocks == 1) {
        return HT_ERROR;
    }

    BF_Block* hashBlock;
    BF_Block_Init(&hashBlock);
    CALL_BF(BF_GetBlock(fileDesc, 0, hashBlock));
    char* hashTable = BF_Block_GetData(hashBlock);
    // 1η Περίπτωση : Για δεδομένο id εκτυπώνουμε τις πληροφορίες του record και το bucket που ανήκει 
    if (id != NULL) {
        int Position = hashFunction(*id, ((HashTable*)hashTable)->depth);
        BF_Block* bucket;
        BF_Block_Init(&bucket);
        int HashPosition = Position / 64;
        for (int i = 0; i < HashPosition; i++) {
            CALL_BF(BF_GetBlock(fileDesc, ((HashTable*)hashTable)->nextHT, hashBlock));
            hashTable = BF_Block_GetData(hashBlock);
        }
        int HashTableS = Position % 64;
        int whichfblock = ((HashTable*)hashTable)->buckets[HashTableS];
        if (whichfblock == -1) {
            printf("ID doesn't exist\n");
            return HT_OK;
        }
        CALL_BF(BF_GetBlock(fileDesc, whichfblock, bucket));
        char* data = BF_Block_GetData(bucket);
        for (int i = 0; i < ((Bucket*)data)->recordCount; i++) {
            Record r = ((Bucket*)data)->records[i];
            if (r.id == *id) {
                printf("ID: %d, name: %s, surname: %s, city: %s, which  bucket : %d\n", r.id, r.name,
                    r.surname, r.city, hashFunction(*id, ((HashTable*)hashTable)->depth) % 64);
            }
        }
        BF_UnpinBlock(bucket);
    } 
    // 2η Περίπτωση : Αν δεν δωθεί κάποιο id τότε εκτυπώνουμε όλα τα record ανά bucket
    else {
        for (int i = 1; i < num_of_blocks; i++) {
            BF_Block* bucketBlock;
            BF_Block_Init(&bucketBlock);
            CALL_BF(BF_GetBlock(fileDesc, i, bucketBlock));
            char* bucket = BF_Block_GetData(bucketBlock);
            for (int j = 0; j < ((Bucket*)bucket)->recordCount; j++) {
                Record r = ((Bucket*)bucket)->records[j];
                if (j==0) {
                  printf("Bucket: %d \n",hashFunction(r.id, ((HashTable*)hashTable)->depth) % 64);
                }
                printf("ID: %d, name: %s, surname: %s, city: %s\n", r.id, r.name,
                    r.surname, r.city);
            }
            BF_UnpinBlock(bucketBlock);
            printf("\n");
        }
    }

    BF_UnpinBlock(hashBlock);
    return HT_OK;
}

