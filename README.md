# SparkySIEM

Spawns from some scratch work in: [https://github.com/jamster88/kafka_expt](https://github.com/jamster88/kafka_expts)

The goal is replicate the **very** basic functions of a Splunk Forwarder:
* point at file (or directory)
* monitor for changes
* send changes to some location - *Kafka in this case*

Eventually the goal is to add more and more features that a present on the Splunk forwarder like:
* employ TLS encryption with certificates to protect comms and verify producer/consumer
* config files
* remote config - *eg call 'home' to a central config server to get their marching orders*
* remote deployment - *deploy with TLS encryption and certs to enable secure remote config*

Finally, the goal will be to enable features that *__aren't__* present in the Splunk forwarder like custom message formats and anything anyone else can think of.


## Consumption

This is designed to send the data to a Kafka instance with the expectation that it will be consumed by a Spark Streaming job, however it could be consumed by other tools like Beam, Flink, etc.


## TO-DO

* Finish the barebones version
* Write tests
* Feature work planning
* *flesh this out*
