#!/usr/bin/env python

import subprocess
import argparse
import os
import socket

def parse_arguments():
    parser = argparse.ArgumentParser(description='Launch master/slave and routing servers for chat service')
    parser.add_argument('image_name', nargs='?', default='test1.jpg')
    parser.add_argument('-m', dest='master_server', action='store_true')
    parser.add_argument('-r', dest='routing_server', action='store_true')
    parser.add_argument('-p', dest='port')
    parser.add_argument('--host', dest='host')
    parser.add_argument('--routing-port', dest='routing_port')
    parser.set_defaults(
        master_server=False,
        routing_server=False,
        slave_server=False,
        host="localhost",
        port="3010",
        routing_port="3010")
    return parser.parse_args()

def start_routing_server(args):
    print 'Starting routing server on ' + get_ip() + ":" + args.port
    subprocess.Popen([os.getcwd() + '/routing_server', '-p', args.port])

def start_master_server(args):
    print 'Starting master (and slave) on ' + get_ip() + ":" + args.port
    print 'Connecting to routing server at ' + args.host + ":" + args.routing_port
    subprocess.Popen([os.getcwd() + '/tsd', '-h', get_ip(), '-p', args.port, '-r', args.host, '-t', args.routing_port])

def get_ip():
    """Returns the external ip address of the current machine"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('10.255.255.255', 1))
        IP = s.getsockname()[0]
    except:
        IP = '127.0.0.1'
    finally:
        s.close()
    return IP

def main():
    args = parse_arguments()
    if args.routing_server:
        start_routing_server(args)
    elif args.master_server:
        start_master_server(args)

if __name__ == '__main__':
    main()