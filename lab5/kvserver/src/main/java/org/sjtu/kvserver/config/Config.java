package org.sjtu.kvserver.config;

import org.I0Itec.zkclient.ZkClient;
import org.I0Itec.zkclient.serialize.SerializableSerializer;

public class Config {

    public final static String clusterPath = "/clusterServer";
    public final static String registryPath = "/serverRegistry";
    public final static String connectString = "172.19.44.153:2181,172.19.44.155:2181,172.19.44.158:2181";

}