# Working with CHIP Tool

The CHIP Tool (`chip-tool`) is a tool that allows to commission a Matter device
into the network and to communicate with it using the Zigbee Cluster Library
(ZCL) messages.

<hr>

-   [Source files](#source)
-   [Building chip-tool](#building)
-   [Using chip-tool for Matter accessory testing](#using)
-   [Supported commands and options](#commands)

<hr>

<a name="source"></a>

## Source files

You can find source files of the CHIP Tool in the `examples/chip-tool`
directory.

<hr>

<a name="building"></a>

## Building

Before you can use the `chip-tool`, you must compile it from the source on Linux
(amd64 / aarch64) or macOS.

> To ensure compatibility, build the `chip-tool` and the Matter device from the
> same revision of the connectedhomeip repository.

To build and run the `chip-tool`:

1. Install all necessary packages and prepare the build system. For more
   details, see the [Building Matter](BUILDING.md) documentation:

    ```
    sudo apt-get update
    sudo apt-get upgrade

    sudo apt-get install git gcc g++ python pkg-config libssl-dev libdbus-1-dev libglib2.0-dev libavahi-client-dev ninja-build python3-venv python3-dev python3-pip unzip libgirepository1.0-dev libcairo2-dev bluez
    ```

    If the `chip-tool` is built on a Raspberry Pi, install additional packages
    and reboot the device:

    ```
    sudo apt-get install pi-bluetooth
    sudo reboot
    ```

2. Clone the Project CHIP repository:

    ```
    git clone https://github.com/project-chip/connectedhomeip.git
    ```

3. Enter the `connectedhomeip` directory:

    ```
    cd connectedhomeip
    ```

4. Initialize the git submodules:

    ```
    git submodule update --init
    ```

5. Run the following command:

    ```
    ./scripts/examples/gn_build_example.sh examples/chip-tool BUILD_PATH
    ```

    `BUILD_PATH` specifies where the target binaries shall to be placed.

6. To run the tool execute the following command:

    ```
    ./BUILD_PATH/chip-tool
    ```

    As a result, a list of available clusters will show up. Each cluster can be
    used as a root of the new command corresponding to ZCL data model
    interaction, for instance:

    ```
    ./BUILD_PATH/chip-tool pairing
    ```

    will print the list of sub-operations available for `pairing` cluster.

    The following is one of the possible final commands which requires
    particular command line arguments:

    ```
    ./BUILD_PATH/chip-tool pairing ble-thread
    ```

    Execution of above line will print an error and hint the proper list of
    parameters which must be provided to run ZCL operation.

<hr>

<a name="using"></a>

## Using `chip-tool` for Matter accessory testing

This section describes how to use `chip-tool` to test the Matter accessory.
Below steps depend on the application clusters that you implemented on the
device side and may be different for your accessory.

### Step 1: Prepare the Matter accessory.

This tutorial is using the
[Matter lighting-app example](https://github.com/project-chip/connectedhomeip/tree/master/examples/lighting-app)
with the Bluetooth LE commissioning. However, you can adapt this procedure to
other available Matter examples.

Build and program the device with the Matter accessory firmware by following the
example's documentation.

### Step 2: Enable Bluetooth LE advertising on Matter accessory device.

Some examples are configured to advertise automatically on boot. Other examples
require physical trigger, for example pushing a button. Follow the documentation
of the Matter accessory example to learn how Bluetooth LE advertising is enabled
for the given example.

### Step 3: Determine network pairing credentials

You must provide the controller with network credentials that will be further
used in the device commissioning procedure to configure the device with a
network interface, such as Thread or Wi-Fi.

#### Thread network credentials

Fetch and store the current Active Operational Dataset from the Thread Border
Router. Depending on if the Thread Border Router is running on Docker or
natively on Raspberry Pi, execute the following commands:

-   For Docker:

    ```
    sudo docker exec -it otbr sh -c "sudo ot-ctl dataset active -x"
    0e080000000000010000000300001335060004001fffe002084fe76e9a8b5edaf50708fde46f999f0698e20510d47f5027a414ffeebaefa92285cc84fa030f4f70656e5468726561642d653439630102e49c0410b92f8c7fbb4f9f3e08492ee3915fbd2f0c0402a0fff8
    Done
    ```

-   For native installation:

    ```
    sudo ot-ctl dataset active -x
    0e080000000000010000000300001335060004001fffe002084fe76e9a8b5edaf50708fde46f999f0698e20510d47f5027a414ffeebaefa92285cc84fa030f4f70656e5468726561642d653439630102e49c0410b92f8c7fbb4f9f3e08492ee3915fbd2f0c0402a0fff8
    Done
    ```

#### Wi-Fi network credentials

The following Wi-Fi network credentials must be determined to commission the
Matter accessory device to the Wi-Fi network in next steps:

-   Wi-Fi SSID
-   Wi-Fi password

Matter specification does not define how the Thread or Wi-Fi credentials are
obtained by Controller.

For example, for Thread, instead of fetching datasets directly from the Thread
Border Router, you might also use a different out-of-band method.

For Wi-Fi, the steps required to determine the SSID and password may vary
depending on the setup. For instance, you might need to contact your local Wi-Fi
network administrator.

### Step 4: Determine Matter accessory device's _discriminator_ and _setup PIN code_

The controller uses a 12-bit value called _discriminator_ to discern between
multiple commissionable device advertisements, as well as a 27-bit _setup PIN
code_ to authenticate the device. You can find these values in the logging
terminal of the device (for instance, UART). For example:

```
I: 254 [DL]Device Configuration:
I: 257 [DL] Serial Number: TEST_SN
I: 260 [DL] Vendor Id: 65521 (0xFFF1)
I: 263 [DL] Product Id: 32768 (0x8000)
I: 267 [DL] Hardware Version: 1
I: 270 [DL] Setup Pin Code: 20202021
I: 273 [DL] Setup Discriminator: 3840 (0xF00)
I: 278 [DL] Manufacturing Date: (not set)
I: 281 [DL] Device Type: 65535 (0xFFFF)
```

In above printout, the _discriminator_ is 3840 (0xF00) and the _setup PIN code_
is equal to 20202021 accordingly.

### Step 5: Commission Matter accessory device into existing network

#### Commissioning into Thread network over Bluetooth LE

To commission the device to the existing Thread network run the following
command:

```
./BUILD_PATH/chip-tool pairing ble-thread <node_id> hex:<operational_dataset> <pin_code> <discriminator>
```

where:

-   _<node_id\>_ is the user-defined ID of the node being commissioned,
-   _<operational_dataset\>_ is the Operational Dataset determined in step 3,
-   _<pin_code\>_ and _<discriminator\>_ are device specific keys determined in
    step 4

#### Commissioning into Wi-Fi network over Bluetooth LE

```
./BUILD_PATH/chip-tool pairing ble-wifi <node_id> <ssid> <password> <pin_code> <discriminator>

```

where:

-   _<node_id\>_ is the user-defined ID of the node being commissioned,
-   _<ssid\>_ and _<password\>_ are credentials determined in step 3,
-   _<pin_code\>_ and _<discriminator\>_ are device specific keys determined in
    step 4

If the hexadecimal format is preferred the `hex:` prefix shall be used, i.e:

```
./BUILD_PATH/chip-tool pairing ble-wifi <node_id> hex:<ssid> hex:<password> <pin_code> <discriminator>

```

The _<node_id>_ can be simply provided as a hexadecimal value with `0x` prefix.

After connecting the device over Bluetooth LE, the controller will go through
the following stages:

-   Establishing a secure connection that completes the PASE
    (Password-Authenticated Session Establishment) session using SPAKE2+
    protocol and results in printing the following log:

        ```
        Secure Session to Device Established
        ```

-   Providing the device with a network interface using ZCL Network
    Commissioning cluster commands, and the network pairing credentials from
    step 3
-   Discovering the IPv6 address of the Matter accessory using the SRP (Service
    Registration Protocol) for Thread devices, or the mDNS (Multicast Domain
    Name System) protocol for Wi-Fi or Ethernet devices. It results in printing
    log that indicates that the node address has been updated. The IPv6 address
    of the device is cached in the controller for later usage.
-   Closing the Bluetooth LE connection, as the commissioning process is
    finished and the `chip-tool` is now using only the IPv6 traffic to reach the
    device.

#### Commissioning over IP

The command below will discover devices and try to pair with the first one it
discovers using the provided _setup PIN code_.

```
./BUILD_PATH/chip-tool pairing onnetwork <node_id> <pin_code>

```

where:

-   _<node_id\>_ is the user-defined ID of the node being commissioned,
-   _<pin_code\>_ is device specific _setup PIN code_ determined in step 4

The command below will discover devices with long discriminator and try to pair
with the first one it discovers using the provided setup code.

```
./BUILD_PATH/chip-tool pairing onnetwork-long <node_id> <pin_code> <discriminator>

```

where:

-   _<node_id\>_ is the user-defined ID of the node being commissioned,
-   _<pin_code\>_ and _<discriminator\>_ are device specific keys determined in
    step 4

The command below will discover devices based on the given QR code payload
(which devices log when they start up) and try to pair with the first one it
discovers.

```
./BUILD_PATH/chip-tool qrcode <node_id> <qrcode_payload>

```

where:

-   _<node_id\>_ is the user-defined ID of the node being commissioned,
-   _<qrcode_payload\>_ is the QR code payload

#### Forget the currently-commissioned device

```
./BUILD_PATH/chip-tool pairing unpair <node_id>

```

where:

-   _<node_id\>_ is the user-defined ID of the node which is going to be forgot
    by the `chip-tool`

#### Note

`chip-tool` currently only supports commissioning and remembering one device at
a time. The configuration state is cached in `/tmp/chip_tool_config.ini`;
deleting this and other .ini files in `/tmp` can sometimes resolve issues
related to stale configuration.

### Step 6: Control application ZCL clusters.

For the lighting-app example, execute the following command to toggle the LED
state:

```
./BUILD_PATH/chip-tool onoff toggle <node_id> <endpoint_id>
```

where:

-   _<node_id\>_ is the user-defined ID of the commissioned node
-   _<endpoint_id\>_ is the ID of the endpoint with OnOff cluster implemented

To change the brightness of the LED, use the following command, with the
_<level\>_ equal to value between 0 and 255.

```
./BUILD_PATH/chip-tool levelcontrol move-to-level <level> <transition_time> <option_mask> <option_override> <node_id> <endpoint_id>
```

where:

-   _<level\>_ is the brightness level encoded between 0 and 255
-   _<transition_time\>_ is the transition time
-   _<option_mask\>_ is the option mask
-   _<option_override\>_ is the option override
-   _<node_id\>_ is the user-defined ID of the commissioned node
-   _<endpoint_id\>_ is the ID of the endpoint with LevelControl cluster
    implemented

In case of doubts regarding any of the options, please refer to the Matter
specification.

### Step 7: Read basic information out of the accessory.

Every Matter accessory device supports a Basic Cluster, which maintains
collection of attributes that a controller can obtain from a device, such as the
vendor name, the product name, or software version. Use `read` command on
`basic` cluster to read those values from the device:

```
./BUILD_PATH/chip-tool basic read vendor-name <node_id> <endpoint_id>
./BUILD_PATH/chip-tool basic read product-name <node_id> <endpoint_id>
./BUILD_PATH/chip-tool basic read software-version <node_id> <endpoint_id>
```

where:

-   _<node_id\>_ is the user-defined ID of the commissioned node
-   _<endpoint_id\>_ is the ID of the endpoint with Basic cluster implemented

Use the following command to list all available commands for Basic cluster:

```
./BUILD_PATH/chip-tool basic
```

<hr>

<a name="commands"></a>

## Supported commands and options

### Print all supported clusters

```
./BUILD_PATH/chip-tool
```

Example output snippet:

```bash

[1647346057.900626][394605:394605] CHIP:TOO: Missing cluster name
Usage:
  ./chip-tool cluster_name command_name [param1 param2 ...]

  +-------------------------------------------------------------------------------------+
  | Clusters:                                                                           |
  +-------------------------------------------------------------------------------------+
  | * accesscontrol                                                                     |
  | * accountlogin                                                                      |
  | * administratorcommissioning                                                        |
  | * alarms                                                                            |
  | * any                                                                               |
  | * appliancecontrol                                                                  |
  | * applianceeventsandalert                                                           |
  | * applianceidentification                                                           |
  | * appliancestatistics                                                               |
  | * applicationbasic                                                                  |

```

### Get the list of commands supported for a specific cluster

```
./BUILD_PATH/chip-tool <cluster_name>
```

where:

-   _<cluster_name\>_ is one of the available clusters (listed in previous
    section)

Example:

```
./BUILD_PATH/chip-tool onoff
```

Output:

```bash
[1647417645.182824][404411:404411] CHIP:TOO: Missing command name
Usage:
  ./chip-tool onoff command_name [param1 param2 ...]

  +-------------------------------------------------------------------------------------+
  | Commands:                                                                           |
  +-------------------------------------------------------------------------------------+
  | * command-by-id                                                                     |
  | * off                                                                               |
  | * on                                                                                |
  | * toggle                                                                            |
  | * off-with-effect                                                                   |
  | * on-with-recall-global-scene                                                       |
  | * on-with-timed-off                                                                 |
  | * read-by-id                                                                        |
  | * read                                                                              |
  | * write-by-id                                                                       |
  | * write                                                                             |
  | * subscribe-by-id                                                                   |
  | * subscribe                                                                         |
  | * read-event-by-id                                                                  |
  | * subscribe-event-by-id                                                             |
  +-------------------------------------------------------------------------------------+
[1647417645.183836][404411:404411] CHIP:TOO: Run command failure: ../../examples/chip-tool/commands/common/Commands.cpp:84: Error 0x0000002F

```

### Get the list of attributes supported for a specific cluster

```
./BUILD_PATH/chip-tool <cluster_name> read
```

where:

-   _<cluster_name\>_ is one of the available clusters (listed in previous
    section)

Example:

```
./BUILD_PATH/chip-tool onoff read
```

Output:

```bash
[1647417857.913942][404444:404444] CHIP:TOO: Missing attribute name
Usage:
  ./chip-tool onoff read attribute-name [param1 param2 ...]

  +-------------------------------------------------------------------------------------+
  | Attributes:                                                                         |
  +-------------------------------------------------------------------------------------+
  | * on-off                                                                            |
  | * global-scene-control                                                              |
  | * on-time                                                                           |
  | * off-wait-time                                                                     |
  | * start-up-on-off                                                                   |
  | * server-generated-command-list                                                     |
  | * client-generated-command-list                                                     |
  | * attribute-list                                                                    |
  | * feature-map                                                                       |
  | * cluster-revision                                                                  |
  +-------------------------------------------------------------------------------------+
[1647417857.914110][404444:404444] CHIP:TOO: Run command failure: ../../examples/chip-tool/commands/common/Commands.cpp:120: Error 0x0000002F
```

### Get the list of command options

To get the list of parameters for a specific command, run the `chip-tool`
executable with the target cluster name and the target command name

Example:

```
./BUILD_PATH/chip-tool onoff on
```

Output:

```bash
[1647417976.556313][404456:404456] CHIP:TOO: InitArgs: Wrong arguments number: 0 instead of 2
Usage:
  ./chip-tool onoff on node-id/group-id endpoint-id-ignored-for-group-commands [--paa-trust-store-path] [--commissioner-name] [--trace_file] [--trace_log] [--ble-adapter] [--timedInteractionTimeoutMs] [--suppressResponse]
[1647417976.556362][404456:404456] CHIP:TOO: Run command failure: ../../examples/chip-tool/commands/common/Commands.cpp:135: Error 0x0000002F

```

#### Selected command options

##### Choosing the Bluetooth adapter

```
--ble-adapter <id>
```

where:

-   _<id\>_ is the id of HCI device

Example:

```
./BUILD_PATH/chip-tool pairing ble-thread 1 hex:0e080000000000010000000300001335060004001fffe002084fe76e9a8b5edaf50708fde46f999f0698e20510d47f5027a414ffeebaefa92285cc84fa030f4f70656e5468726561642d653439630102e49c0410b92f8c7fbb4f9f3e08492ee3915fbd2f0c0402a0fff8 20202021 3840 --ble-adapter 0
```

##### Using message tracing

Message tracing allows to capture `chip-tool` secure messages which can be used
for test automation.

There are additional flags which control where the traces should go:

`--trace_file <filename>` where:

-   _<filename\>_ - is the file where trace data is stored in

For example:

```
./BUILD_PATH/chip-tool pairing <pairing_options> --trace_file <filename>
```

`--trace_log <onoff>` where:

-   _<onoff\>_ is [0/1] flag (when set to 1 the trace data with automation logs
    will be printed to the console)

### Running a test suite against a paired peer device

`chip-tool` allows to run a set of tests, already compiled in the tool, against
a paired Matter device. To get the list of available tests run:

```
./BUILD_PATH/chip-tool tests
```

To execute particular test against the paired device run:

```
./BUILD_PATH/chip-tool tests <test_name>
```

where:

-   _<test_name\>_ is the name of a particular test

### Parsing the setup payload

`chip-tool` offers a utility for parsing the Matter onboarding setup payload to
the human readable form. The payload may be printed e.g. on the device console
during boot-up.

To parse a setup code use the `payload` command with `parse-setup-payload`
sub-command:

```
./BUILD_PATH/chip-tool payload parse-setup-payload <payload>
```

where:

-   _<payload\>_ is the payload to be parsed

Concrete examples:

-   Setup QR code payload:

```
./BUILD_PATH/chip-tool payload parse-setup-payload MT:6FCJ142C00KA0648G00
```

-   Manual pairing code:

```
./BUILD_PATH/chip-tool payload parse-setup-payload 34970112332
```

### Parsing additional data payload

Additional data payload can be parsed with the following command:

```
./BUILD_PATH/chip-tool parse-additional-data-payload <payload>
```

where:

-   _<payload\>_ is the payload with additional data to be parsed

### Discover actions

The command `discover` can be used to resolve node ID and discover available
Matter devices. The following command will print available sub-commands of
`discover`:

```
./BUILD_PATH/chip-tool discover
```

#### Resolving node name

To resolve DNS-SD name corresponding with the given Node ID and update address
of the node in the device controller use the following command:

```
./BUILD_PATH/chip-tool discover resolve <node_id> <fabric_id>
```

where:

-   _<node_id\>_ is the ID of node to be resolved
-   _<fabric_id\>_ is the ID of the fabric where the node belongs to

#### Discovering available Matter accessory devices

To discover all Matter commissionables available in the operation area use the
following command:

```
./BUILD_PATH/chip-tool discover commissionables
```

#### Discovering available Matter commissioners

To discover all Matter commissioners available in the operation area use the
following command:

```
./BUILD_PATH/chip-tool discover commissioners
```

### Pairing

The `pairing` option supports different means regarding Matter device
commissioning procedure.

Thread and Wi-Fi commissioning use-cases were described in
[Using chip-tool for Matter accessory testing](#using) paragraph.

To list all `pairing` sub-commands use the following command:

```
./BUILD_PATH/chip-tool pairing
```

### Interacting with ZCL clusters

As mentioned in [Using chip-tool for Matter accessory testing](#using)
paragraph, executing `chip-tool` with particular cluster name shall list all
operations supported for this cluster:

```
./BUILD_PATH/chip-tool <cluster_name>
```

Example:

```
./BUILD_PATH/chip-tool binding
```

Output:

```bash
[1647502596.396184][411686:411686] CHIP:TOO: Missing command name
Usage:
  ./chip-tool binding command_name [param1 param2 ...]

  +-------------------------------------------------------------------------------------+
  | Commands:                                                                           |
  +-------------------------------------------------------------------------------------+
  | * command-by-id                                                                     |
  | * read-by-id                                                                        |
  | * read                                                                              |
  | * write-by-id                                                                       |
  | * write                                                                             |
  | * subscribe-by-id                                                                   |
  | * subscribe                                                                         |
  | * read-event-by-id                                                                  |
  | * subscribe-event-by-id                                                             |
  +-------------------------------------------------------------------------------------+
[1647502596.396299][411686:411686] CHIP:TOO: Run command failure: ../../examples/chip-tool/commands/common/Commands.cpp:84: Error 0x0000002F
```

According to above list, the `binding` cluster supports operations such as e.g.
read or write. Attributes from that cluster can also be subscribed by the
controller, which means that the `chip-tool` will receive notifications, for
instance: in case the attribute value is changed or a particular event happens.

#### Examples:

-   Writing exemplary ACL (Access Control List) to the `accesscontrol` cluster:

```
./BUILD_PATH/chip-tool accesscontrol write acl '[{"fabricIndex": 1, "privilege": 5, "authMode": 2, "subjects": [112233], "targets": null}, {"fabricIndex": 1, "privilege": 3, "authMode": 2, "subjects": [<node1_id>], "targets": [{"cluster": 6, "endpoint": 1, "deviceType": null}, {"cluster": 8, "endpoint": 1, "deviceType": null}]}]' <node_id> 0
```

where:

-   _<node1_id\>_ is the ID of the node which requests a permission to send
    commands to <node2_id\>\*
-   _<node2_id\>_ is the ID of the node which is supposed to receive commands
    from <node1_id\>\*

-   Adding a binding table to the `binding` cluster:

```
./BUILD_PATH/chip-tool binding write binding '[{"fabricIndex": 1, "node": <node1_id>, "endpoint": 1, "cluster": 6}, {"fabricIndex": 1, "node": <node_id>, "endpoint": 1, "cluster": 8}]' <node2_id> 1
```

where:

-   _<node1_id\>_ is the ID of the first node
-   _<node2_id\>_ is the ID of the second node
