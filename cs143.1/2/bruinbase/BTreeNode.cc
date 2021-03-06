#include "BTreeNode.h"

using namespace std;

/*
 * Read the content of the node from the page pid in the PageFile pf.
 * @param pid[IN] the PageId to read
 * @param pf[IN] PageFile to read from
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::read(PageId pid, const PageFile& pf)
{ 
  // Use PageFile::read to read into buffer
  RC rc = pf.read(pid, buffer);
  return rc; 
}
    
/*
 * Write the content of the node to the page pid in the PageFile pf.
 * @param pid[IN] the PageId to write to
 * @param pf[IN] PageFile to write to
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::write(PageId pid, PageFile& pf)
{
  // Use PageFile::write to write from the buffer
  RC rc = pf.write(pid, buffer);
  return rc; 
}
/*
 * An entry of all -1 means an empty entry.
 * The first entry is set to all -2 to signify that the node is a leaf node.
 * A -1 PageId in the last 4 bytes means it's the last leaf node.
 *
 * Current search algorithm is linear.
 *
 * BTreeIndex must:
 * - initialize each leaf node before inserting into it
 * - for insertAndSplit, assign the new pointer for the node being split
 */ 

/*
 * Return the number of keys stored in the node.
 * @return the number of keys in the node
 */
int BTLeafNode::getKeyCount()
{ 
  int numKeys = 0;
  int limit = PageFile::PAGE_SIZE - sizeof(PageId);
  int key;
  RecordId rid;

  // Last 4 bytes are a PageId pointer to next leaf node
  for (int index = LEAF_FIRST_ENTRY * LEAF_ENTRY_SIZE; index < limit; index += LEAF_ENTRY_SIZE)
  {
    convertToLeafEntry(buffer, index, key, rid);

    // All -1's means empty entry
    if (rid.pid == -1 && rid.sid == -1 && key == -1)
    {
	break;
    }

    numKeys++;
  }

  return numKeys; 
}

/*
 * Insert a (key, rid) pair to the node.
 * @param key[IN] the key to insert
 * @param rid[IN] the RecordId to insert
 * @return 0 if successful. Return an error code if the node is full.
 */
RC BTLeafNode::insert(int key, const RecordId& rid)
{
  int numKeys = this->getKeyCount();

  // Check if the node is already full  
  if (numKeys >= LEAF_MAX_ENTRIES)
  {
    return RC_NODE_FULL;
  }

  int eid;
  int oKey;
  RecordId oRid;

  // Find the spot to insert
  RC rc = locate(key, eid);

  // No key >= searchKey, so insert at end
  if (rc != 0)
  {
    eid = numKeys;
  }

  // Move all chars in the buffer by the size of one leaf entry
  for (int i = numKeys * LEAF_ENTRY_SIZE - 1; i >= eid * LEAF_ENTRY_SIZE; i--)
  {
    buffer[i+LEAF_ENTRY_SIZE] = buffer[i];
  }

  char buf[LEAF_ENTRY_SIZE];

  // Convert the (key, rid) pair to a char array
  convertToChar(key, rid, buf);
 
  // Insert the new entry
  for (int i = eid * LEAF_ENTRY_SIZE; i < eid * LEAF_ENTRY_SIZE + LEAF_ENTRY_SIZE; i++)
  {
    buffer[i] = buf[i - eid * LEAF_ENTRY_SIZE];
  }
 
  return 0;
}

/*
 * Insert the (key, rid) pair to the node
 * and split the node half and half with sibling.
 * The first key of the sibling node is returned in siblingKey.
 * @param key[IN] the key to insert.
 * @param rid[IN] the RecordId to insert.
 * @param sibling[IN] the sibling node to split with. This node MUST be EMPTY when this function is called.
 * @param siblingKey[OUT] the first key in the sibling node after split.
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::insertAndSplit(int key, const RecordId& rid, 
                              BTLeafNode& sibling, int& siblingKey)
{ 
  // Make sure the node is full
  if (getKeyCount() < LEAF_MAX_ENTRIES)
  {
    return RC_INVALID_FILE_FORMAT;
  }

  // The entry id of the last entry before the split
  int spid = (int) (LEAF_MAX_ENTRIES / 2 + 1);
  int oKey;
  RecordId oRid;
  int eKey = -1;
  RecordId eRid = {-1, -1};

  for (int i = spid*LEAF_ENTRY_SIZE; i < LEAF_MAX_ENTRIES*LEAF_ENTRY_SIZE; i += LEAF_ENTRY_SIZE)
  {
    convertToLeafEntry(buffer, i, oKey, oRid);
    
    // Copy over the entries that will now be in sibling
    sibling.insert(oKey, oRid);

    // Assign the sibling key (will be changed if necessary)
    if (i == spid*LEAF_ENTRY_SIZE)
    {
	siblingKey = oKey;
    }

    char buf[LEAF_ENTRY_SIZE];
    convertToChar(eKey, eRid, buf);
     
    // Turn this entry in the current leaf to an empty entry
    for (int j = i; j < i + LEAF_ENTRY_SIZE; j++)
    {
	buffer[j] = buf[j-i];
    }
  }

  // Set the sibling's node pointer
  sibling.setNextNodePtr(getNextNodePtr()); 

  // Now insert the new record
  if (key < siblingKey)
  {
    insert(key, rid);
  } else
  {
    sibling.insert(key, rid);
  }

  return 0;
}

/*
 * Find the entry whose key value is larger than or equal to searchKey
 * and output the eid (entry number) whose key value >= searchKey.
 * Remeber that all keys inside a B+tree node should be kept sorted.
 * @param searchKey[IN] the key to search for
 * @param eid[OUT] the entry number that contains a key larger than or equalty to searchKey
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::locate(int searchKey, int& eid)
{ 
  int numKeys = getKeyCount();

  int oKey;
  RecordId oRid;

  for (int i = LEAF_FIRST_ENTRY; i < numKeys; i++)
  {
    convertToLeafEntry(buffer, i * LEAF_ENTRY_SIZE, oKey, oRid);

    if (oKey >= searchKey)
    {
	eid = i;
        return 0;
    }
  }
 
  return RC_NO_SUCH_RECORD;
}

/*
 * Read the (key, rid) pair from the eid entry.
 * @param eid[IN] the entry number to read the (key, rid) pair from
 * @param key[OUT] the key from the entry
 * @param rid[OUT] the RecordId from the entry
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::readEntry(int eid, int& key, RecordId& rid)
{ 
  if (eid < 0 || eid >= getKeyCount())
  {
    return RC_INVALID_CURSOR;
  }

  convertToLeafEntry(buffer, eid*LEAF_ENTRY_SIZE, key, rid);

  return 0;
}

/*
 * Return the pid of the next slibling node.
 * @return the PageId of the next sibling node 
 */
PageId BTLeafNode::getNextNodePtr()
{
  return this->getNextNodePtrHelp(buffer); 
}

PageId BTLeafNode::getNextNodePtrHelp(char* buffer)
{
  int* bint = (int*) buffer;
  return *(bint+255);
}

/*
 * Set the pid of the next slibling node.
 * @param pid[IN] the PageId of the next sibling node 
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::setNextNodePtr(PageId pid)
{ 
  this->setNextNodePtrHelp(buffer, pid); 

  return 0; 
}

void BTLeafNode::setNextNodePtrHelp(char* buffer, PageId pid)
{
  int* bint = (int*) buffer;
  *(bint+255) = pid;
  buffer = (char*) bint;
}

void BTLeafNode::convertToLeafEntry(char* buffer, int index, int& key, RecordId& rid)
{
  int* bint = (int*) buffer;
  rid.pid = *(bint+index/sizeof(int));
  rid.sid = *(bint+index/sizeof(int)+1);
  key = *(bint+index/sizeof(int)+2);
}

void BTLeafNode::convertToChar(int key, RecordId rid, char* buf)
{
    int* bint = (int*) buf;
    *(bint) = rid.pid;
    *(bint+1) = rid.sid;
    *(bint+2) = key;
    buf = (char*) bint;
}

void BTLeafNode::initialize()
{
  int eKey = -1;
  RecordId eRid = {-1, -1};
  char buf[LEAF_ENTRY_SIZE];  

  // Set all entries to empty
  for (int i = 0; i < LEAF_MAX_ENTRIES; i++)
  {
     convertToChar(eKey, eRid, buf);
	
     for (int j = 0; j < LEAF_ENTRY_SIZE; j++)
     {
	buffer[i*LEAF_ENTRY_SIZE+j] = buf[j];	
     }
  }

  // Set the next node pointer to empty (i.e. null pointer)
  this->setNextNodePtr(-1);
}





/*********************************************************************

             BTNonLeafNode methods

***********************************************************************/

PageId BTNonLeafNode::getStartPid() const { return *((PageId*)data); }

void BTNonLeafNode::setStartPid(PageId pid) { 
  PageId* start_pid = (PageId*) data;
  *start_pid = pid;
}

NonLeafData* BTNonLeafNode::getStartOfBuffer() {
  char* start = (char*)data + sizeof(PageId);          // The first 4 bytes are reserved for the first pid
  return (NonLeafData*)start;
}


/*
 * Read the content of the node from the page pid in the PageFile pf.
 * @param pid[IN] the PageId to read
 * @param pf[IN] PageFile to read from
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::read(PageId pid, const PageFile& pf)
{ 
      // Copy the contents of the page with id pid from PageFile pf into the internal buffer.
      // Open the page file
  
  RC read_status = pf.read(pid, data);
  
  // Go through the buffer and calculate how many valid pages do we have?
  
  // Make sure that read was successful
  return read_status;
  
}
    
/*
 * Write the content of the node to the page pid in the PageFile pf.
 * @param pid[IN] the PageId to write to
 * @param pf[IN] PageFile to write to
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::write(PageId pid, PageFile& pf)
{ 
  
  
  // Take the contents of the buffer and write it into the page file at the appropriate pid location
  RC write_status = pf.write(pid, data);
  
  return write_status;
}


/*
 * Return the number of keys stored in the node.
 * @return the number of keys in the node
 */
int BTNonLeafNode::getKeyCount()
{ 
  
  // Go through the buffer, convert things to NonLeafData structs, then count how many until 
  // we get to pid = -1 or the end of the buffer.

  NonLeafData* start = this->getStartOfBuffer();
  int count = 0;
  
  PageId start_pid = this->getStartPid();
  
  if(start_pid == -1) 
    return 0;
  
  char* endPoint = data + PageFile::PAGE_SIZE - sizeof(PageId);

  while(start->right_pid != -1 && (char*) start < endPoint){
    count++;

    // This should increment by the size of NonLeafData struct....
    start++;
  }
  
  return count; 
}


/*
 * Insert a (key, pid) pair to the node.
 * @param key[IN] the key to insert
 * @param pid[IN] the PageId to insert
 * @return 0 if successful. Return an error code if the node is full.
 */
RC BTNonLeafNode::insert(int key, PageId pid)
{ 

  // Make sure that we have enough room to insert another (key, pid) pair.
  int MAX_PAIR_COUNT = MAX_NONLEAFDATA;
  int currentCount = this->getKeyCount();
  
  if(currentCount >= MAX_PAIR_COUNT)
    return RC_NODE_FULL;

  // Go through the page and point to where
  // find the position to insert the pid

   NonLeafData* start = this->getStartOfBuffer();
  int count = 0;

  while(start->right_pid != -1 && (char*) start < data + PageFile::PAGE_SIZE - sizeof(PageId)
	&& start->key < key ){ 
    

      // This should increment by the size of NonLeafData struct....
      start++;
      count++;
  }

  // Shift everything down, if necessary.

  if(start->right_pid != -1)  {
      // Shift down the elements...

    int num_elements = MAX_NONLEAFDATA - count;
    shift((char*)start, num_elements);
  }

  // and now insert...
  start->right_pid = pid;
  start->key = key;


  
  return 0;
}


// NonLeafData* BTNonLeafNode::find() {

//   while
  

// }


/*
 * Insert the (key, pid) pair to the node
 * and split the node half and half with sibling.
 * The middle key after the split is returned in midKey.
 * @param key[IN] the key to insert
 * @param pid[IN] the PageId to insert
 * @param sibling[IN] the sibling node to split with. This node MUST be empty when this function is called.
 * @param midKey[OUT] the key in the middle after the split. This key should be inserted to the parent node.
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::insertAndSplit(int key, PageId pid, BTNonLeafNode& sibling, int& midKey)
{ 
 
  // DEBUG 
  //  printf("Inserting an spliting with key = %d\n", key);

  // Go to the median value
  int num_keys = this->getKeyCount();
  int median_pos = num_keys / 2;
  
   // Make sure the node is full
  if (num_keys < MAX_NONLEAFDATA)
  {
    return RC_INVALID_FILE_FORMAT;
  }
  
  NonLeafData* start = this->getStartOfBuffer();
  
  // Insert everything on the right into the sibling node
  
  // Go to the median position
  // median_pos should be bounded via getKeyCount
  // except for the special case where there are zero pairs.

  // Take the median value out, then insert its pid pointer as the start_pid for the 
  // sibling.
  
  NonLeafData* median_pair = start + median_pos;
  
  midKey = median_pair->key;
  sibling.initializeRoot(median_pair->right_pid, -1, -1);

  
  for(int i = median_pos + 1; i < num_keys; i++) {
    
    // Insert this guy into the sibling
    int current_pid = (start + i)->right_pid;
    int current_key = (start + i)->key;

    
      RC insert_status = sibling.insert(current_key, current_pid);
      
    if(insert_status != 0) {
      return insert_status;
    }
    
    // Clear the current slot's value
    (start + i)->right_pid = -1;
    (start + i)->key = -1;
    
  }
  
  // Clear the median key
  (start + median_pos)->right_pid = -1;
  (start + median_pos)->key = -1;
  
  
  // Insert the pair.
  
  // Figure out where the pair belongs, before or after the median
  bool insertHere = (key < midKey) ? true : false;
  RC insert_status;
  if(insertHere) {
    insert_status = this->insert(key, pid);
  }else {
    insert_status = sibling.insert(key, pid);
  }

  return 0;
}

/*
 * Given the searchKey, find the child-node pointer to follow and
 * output it in pid.
 * @param searchKey[IN] the searchKey that is being looked up.
 * @param pid[OUT] the pointer to the child node to follow.
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::locateChildPtr(int searchKey, PageId& pid)
{ 
  
  // Look through the keys until we get a match
  
  NonLeafData* start = this->getStartOfBuffer();
  

  int i = 0;
  for(; i < this->getKeyCount(); i++){
    
    // Search for the key, if we find a match, record
    // the pid and then stop looking
    if( (start+i)->key > searchKey) {
     
      // Needs to return the previous NonLeafData's pid, because the 
      // pid's are attached to the right. Meaning the pid attached to 
      // a NonLeafData is the pid if the searchKey is greater than the 
      // NonLeafData's key.

      // If we are at the beginning of the list, then we will return the saved
      // start_pid
      
      if(i == 0) {
	pid = this->getStartPid();
      }else {
	
	// We want the previous NonLeafData's pid, since pid's are attached to the right side.
	pid = (start+i - 1)->right_pid;
      }
      
      
      return 0;
    }

  }


  pid = (start+i-1)->right_pid;

  return 0; 
}

/*
 * Initialize the root node with (pid1, key, pid2).
 * @param pid1[IN] the first PageId to insert
 * @param key[IN] the key that should be inserted between the two PageIds
 * @param pid2[IN] the PageId to insert behind the key
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::initializeRoot(PageId pid1, int key, PageId pid2)
{
  
  
   
   NonLeafData* start = this->getStartOfBuffer();

  // Init the buffer (page) with default values. if pid = -1, it means that
  // this is the end of the buffer. Therefore we will set everything to -1.
  
  for(int i = 0; i < MAX_NONLEAFDATA; i++) {
    (start+i)->right_pid = -1;
    (start+i)->key = -1;
  }
  
  // Add the values
  // Add the first pid value
  PageId* first_pid = (PageId*)data;
  *first_pid = pid1;
  
  
  start->right_pid = pid2;
  start->key = key;
  
 return 0; 

}


void BTNonLeafNode::shift(const char* buffer, unsigned int size) {

  
  NonLeafData* start = (NonLeafData*)buffer;

  // Start with the end
  unsigned int STOP = 0;

  // i = size - 1 because we want to move things to the right, thus the last element is thrown away.
  // We are starting from the second to last element.
  for(unsigned int i = size - 1; i > STOP; i--){

    NonLeafData* end = ((NonLeafData*) buffer)+i;
    NonLeafData* head = ((NonLeafData*) buffer)+i-1;

    // Copy the values
    end->right_pid = head->right_pid;
    end->key = head->key;
  }
}

