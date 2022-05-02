# LinuxDevice
Development of a `Linux Character Device` using concurrent programming to thread stream buffers on kernel memory. The buffers are streamed in the kernel memory using kernel threads in the form of a producer (writer) - consumer (reader) problem. That is, the writer threads are generating a given length buffer in the kernel memory that is available to be populated from a producer process in the user process. The producer process communicates using shared memmory segments with another user space process - consumer that consumes (reads) the already generated buffer utilizing reader kernel threads organized in the linux kernel device. For the coordination of the writer - reader kernel threads basic linux synchronization primitives are used including semaphore and mutex locks. Further optimization can be acheived by turning on the `WQ_FLAG_EXCLUSIVE` flag in order to make sure to free only 1 thread to consume 1 exactly resource at a time. This option works well for specific type of problems when a single process/thread is sufficient to completely serve the resource once it becomes available. However, for typical linux device applications this option is turned off and instead the classical `wake up` is used to release all the sleeping processing from the waiting queue at once.




## Compilation Instructions:
---------------------------
Just run `make` to create the scullbuffer.ko and the producer/consumer programs.


## Running Instructions:
-----------------------
1. Make sure that the device is unloaded or the device with same name doesn't exist. Type `sudo ./scull_unload`.

2. Load the scull buffer using command `sudo ./scull_load`. This will create a device called /dev/scullbuffer.

3. Change permissions on this device typing `sudo chmod 777 /dev/scullbuffer`.

4. In case the number of items have to be specified by the user then type `sudo ./scull_load nitems=<nitems>`

5. Type `make` command to compile all relevant files.

	
## Output Format:
----------------

1. The producers each write the item they have produced into their respective log files. 

2. The log file of a producer has the following format: Prod<third-arg>.log where the <third-arg> is the parameter passed to the producer which is the name of
   item that a particular producer will produce. For example: if producer writes items called "blue" then its output would be in file Prodblue.log.

3. The consumers each write the item they have consumed into their respective log files. The log file of a consumer has the following format: Cons<second-arg>.log
   where <second-arg> is the parameter passed to the consumer program which is an id entifier for a consumer. This argument must be unique per consumer.

## Test Case Description:
------------------------
Before running the testcases make sure to set permission on /dev/scullbuffer as sudo chmod 777 /dev/scullbuffer.

1. 	Testcase #1: 
	-------------
        sudo ./scull_load nitems=50

	This test case consists of a single producer consumer each of which produce and consume 50 items respectively.
   
   	Expected Output: 
	----------------

	The producer and consumer must exit normally and the consumer must consume all the 50 items.
   
	Running instructions: 
	----------------------
        Open two terminals and navigate to "scullbuffer-xsme-s22-csci5103-01" directory where all the source files are located
        Terminal1: ./producer 50 ITEM > TCASE1.out
	Terminal2: ./consumer 50 ITEM >> TCASE1.out 

        Output & Log Files:
	-------------------
	Output file: TCASE1.out
        Log files: Prod_ITEM.log, Cons_ITEM.log 
	

2. 	Testcase #2: 
	-------------
        sudo ./scull_load nitems=50

	This test case consists of a single producer and consumer. The producer will try to produce 50 items before exiting and the consumer consumes
	only 10 items and exits. 
   
	Expected Output: 
	-----------------
	Depending on the size of the scull buffer the producer can deposit scull_b_nr_items + 10 items into the scull buffer and exit once the
	consumer exits. The consumer will read 10 items and exit.
   
	Running Instructions:
	----------------------
        Open two terminals and navigate to "scullbuffer-xsme-s22-csci5103-01" directory where all the source files are located
        Terminal1: ./producer 50 SOME > TCASE2.out
	Terminal2: ./consumer 50 SOME >> TCASE2.out 

        Output & Log Files:
	-------------------
	Output file: TCASE2.out
        Log files: Prod_SOME.log, Cons_SOME.log 

	
3.	Testcase #3:
	-------------
        sudo ./scull_load nitems=100

	This test case consists of a single producer and consumer. The producer produces 50 items before exiting and the consumer consumes 100 items before exiting.

	Expected Output:
	-----------------
	The producer will write 50 items into the scull buffer regardless of the scull buffer size. The consumer will consumer 50 items before exiting since there are         no producers.

	Running Instructions:
	----------------------
        Open two terminals and navigate to "scullbuffer-xsme-s22-csci5103-01" directory where all the source files are located
        Terminal1: ./producer 50 BLACK > TCASE3.out
	Terminal2: ./consumer 100 BLACK >> TCASE3.out 

         
        Output & Log Files:
	-------------------
	Output file: TCASE3.out
        Log files: Prod_BLACK.log, Cons_BLACK.log 

        Discussion:
        -----------
	Consumer consumes a maximum of 50 produced items and does not consume anything after this point.


4.	Testcase #4:
	-------------
        sudo ./scull_load nitems=200

	This test consists of two producers and one consumer. Each producer produces 50 items and the consumer will try to consumer 200 items
	before exiting.

	Expected Output:
	-----------------
	Both the items will produce 50 items regardless of the scullbuffer size and the consumer will exit after consuming 100 items.

	Running Instructions:
	----------------------
        Open three terminals and navigate to "scullbuffer-xsme-s22-csci5103-01" directory where all the source files are located
        Terminal1: ./producer 50 RED1_ > TCASE4.out
	Terminal2: ./producer 50 RED2_ >> TCASE4.out
	Terminal2: ./consumer 200 RED >> TCASE4.out 

        Output & Log Files:
	-------------------
	Output file: TCASE4.out
        Log files: Prod_RED1_.log, Prod_RED2_.log, Cons_RED.log 

        Discussion:
        -----------
	Consumer consumes a maximum of 100 produced items and does not consume anything after this point.

5.	Testcase #5:
	------------
	This test consists of one producer and two consumers. The producer will produce 50 items and the two consumers together (or individuall) will consume the 50           items.

	Expected Output:
	----------------

	The producer will produce 50 items regardless of the scullbuffer size. The consumers will consumer variable number of items that add upto 50 items combined.

	Running Instructions:
	---------------------
        Open three terminals and navigate to "scullbuffer-xsme-s22-csci5103-01" directory where all the source files are located
        Terminal1: ./producer 50 GREEN > TCASE5.out
	Terminal2: ./consumer 25 GR1_ >> TCASE5.out
	Terminal2: ./consumer 25 GR2_ >> TCASE5.out 

        Output & Log Files:
	-------------------
	Output file: TCASE5.out
        Log files: Prod_GREEN_.log, Cons_GR1_.log, Cons_GR2_.log 

        Discussion:
        -----------
	Producer produces 100 GREEN items from which [0 - 25] are read by consumer 1 [26 - 49] are read by consumer 2
	
	
	## References:
	--------------
	The source code in this repository can be freely used, adapted, and redistributed in source or binary form, so long as an acknowledgment appears in derived 	    source files. This acknowledges that part of the source code inpired and inheritted from the book "Linux Device Drivers" by Alessandro Rubini and Jonathan 		Corbet, published by O'Reilly & Associates. No warranty is attached; we cannot take responsibility for errors or fitness for use.
	
	## Contact Information:
	------------------------
	Author Name: Petros Apostolou
	Email: apost035@umn.edu, trs.apostolou@gmail.com
