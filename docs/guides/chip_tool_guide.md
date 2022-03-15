# Working with CHIP Tool

The CHIP Tool (`chip-tool`) is a tool that allows to commission a Matter device
into the network and to communicate with it using the Zigbee Cluster Library
(ZCL) messages.

<hr>

-   [Source files](#source)
-   [Building chip-tool](#building)
-   [Using chip-tool for Matter accessory testing](#using)
-   [List of commands](#commands)

<hr>

<a name="source"></a>

## Source files

You can find source files of the CHIP Tool in the
`examples/chip-tool` directory.

<hr>

<a name="building"></a>

## Building

Before you can use the `chip-tool`, you must compile it from the source on
Linux (amd64 / aarch64) or macOS.

> To ensure compatibility, build the `chip-tool` and the Matter
> device from the same revision of the connectedhomeip repository.

To build and run the `chip-tool`:

1. Install all necessary packages and prepare the build system. For more
   details, see the [Building Matter](BUILDING.md) documentation:

    ```
    sudo apt-get update
    sudo apt-get upgrade

    sudo apt-get install git gcc g++ python pkg-config libssl-dev libdbus-1-dev libglib2.0-dev libavahi-client-dev ninja-build python3-venv python3-dev python3-pip unzip libgirepository1.0-dev libcairo2-dev bluez
    ```

    If the `chip-tool` is built on a Raspberry Pi, install additional
    packages and reboot the device:

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
    As a result, a list of available clusters will show up.
    Each cluster can be used as a root of the new command
    corresonding to ZCL data model interaction, for instance:

    ```
    ./BUILD_PATH/chip-tool pairing
    ```
    will print the list of sub-operations available for `pairing` cluster.

    The following is one of the possible final commands
    which requires particular command line arguments:

    ```
    ./BUILD_PATH/chip-tool pairing ble-thread
    ```

    Execution of above line will print an error and hint the proper
    list of parameters which must be provided to run ZCL operation.

### Using message tracing

Message tracing allows to capture `chip-tool` secure messages which can be used for test
automation.

There are additional flags which control where the traces should go:

-   `--trace_file` outputs trace data to the specified file <filename>

For example:

```
./BUILD_PATH/chip-tool pairing <pairing_options> --trace_file <filename>
```

-   `--trace_log <0/1>` outputs trace data with automation logs to the console (when set to 1)

<hr>

<a name="using"></a>

## Using `chip-tool` for Matter accessory testing

This section describes how to use `chip-tool` to test the Matter
accessory. Below steps depend on the application clusters that you implemented
on the device side and may be different for your accessory.

### Step 1: Prepare the Matter accessory.

This tutorial is using the (Matter lighting-app example)
[https://github.com/project-chip/connectedhomeip/tree/master/examples/lighting-app]
with the Bluetooth LE commissioning. However, you can adapt this
procedure to other available Matter examples.

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

- For Docker:

    ```
    sudo docker exec -it otbr sh -c "sudo ot-ctl dataset active -x"
    0e080000000000010000000300001335060004001fffe002084fe76e9a8b5edaf50708fde46f999f0698e20510d47f5027a414ffeebaefa92285cc84fa030f4f70656e5468726561642d653439630102e49c0410b92f8c7fbb4f9f3e08492ee3915fbd2f0c0402a0fff8
    Done
    ```

- For native installation:

    ```
    sudo ot-ctl dataset active -x
    0e080000000000010000000300001335060004001fffe002084fe76e9a8b5edaf50708fde46f999f0698e20510d47f5027a414ffeebaefa92285cc84fa030f4f70656e5468726561642d653439630102e49c0410b92f8c7fbb4f9f3e08492ee3915fbd2f0c0402a0fff8
    Done
    ```

#### Wi-Fi network credentials

The following Wi-Fi network credentials must be determined to commission
the Matter accessory device to the Wi-Fi network in next steps:

- Wi-Fi SSID
- Wi-Fi password

Matter specification does not define how the Thread or Wi-Fi credentials are
obtained by Controller.

For example, for Thread, instead of fetching datasets directly from the
Thread Border Router, you might also use a different out-of-band method.

For Wi-Fi, the steps required to determine the SSID and password may vary
depending on the setup. For instance, you might need to contact your local
Wi-Fi network administrator.

### Step 4: Determine Matter accessory device's **discriminator** and **setup PIN code**

The controller uses a 12-bit value called **discriminator** to discern between
multiple commissionable device advertisements, as well as a 27-bit **setup PIN code**
to authenticate the device. You can find these values in the logging
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

In above printout, the **discriminator** is 3840 (0xF00)
and the **setup PIN code** is equal to 20202021 accordingly.

### Step 5: Commission Matter accessory device into existing network

#### Commissioning into Thread network over Bluetooth LE

To commission the device to the existing Thread network run the following command:

```
./BUILD_PATH/chip-tool pairing ble-thread <node_id> hex:<operational_dataset> <pin_code> <disciminator>
```

where:
*<node_id>* is the user-defined ID of the node being commissioned,
*<operational_dataset>* is the Operational Dataset determined in step 3,
*<pin_code>* and *<disciminator>* are device specific keys determined in step 4

#### Commissioning into Wi-Fi network over Bluetooth LE

```
./BUILD_PATH/chip-tool pairing ble-wifi <node_id> <ssid> <password> <pin_code> <disciminator>

```
where:
*<node_id>* is the user-defined ID of the node being commissioned,
*<ssid>* and *<password>* are credentials determined in step 3,
*<pin_code>* and *<disciminator>* are device specific keys determined in step 4

If the hexadecimal format is preffered the `hex:` prefix shall be used, i.e:

```
./BUILD_PATH/chip-tool pairing ble-wifi <node_id> hex:<ssid> hex:<password> <pin_code> <disciminator>

```
The *<node_id>* can be simply provided as a hexadecimal value with `0x` prefix.

After connecting the device over Bluetooth LE, the controller will go through
the following stages:

-   Establishing a secure connection that completes the PASE
    (Password-Authenticated Session Establishment) session using SPAKE2+
    protocol and results in printing the following log:

        ```
        Secure Session to Device Established
        ```

-   Providing the device with a network interface using ZCL Network
    Commissioning cluster commands, and the network pairing credentials from step 3
-   Discovering the IPv6 address of the Matter accessory using the SRP (Service
    Registration Protocol) for Thread devices, or the mDNS (Multicast Domain
    Name System) protocol for Wi-Fi or Ethernet devices. It results in printing
    log that indicates that the node address has been updated. The IPv6 address
    of the device is cached in the controller for later usage.
-   Closing the Bluetooth LE connection, as the commissioning process is
    finished and the `chip-tool` is now using only the IPv6 traffic
    to reach the device.

#### Commissioning over IP

The command below will discover devices and try to pair with the first one
it discovers using the provided **setup PIN code**.

```
./BUILD_PATH/chip-tool pairing onnetwork <node_id> <pin_code>

```
where:
*<node_id>* is the user-defined ID of the node being commissioned,
*<pin_code>* is device specific **setup PIN code** determined in step 4

The command below will discover devices with long discriminator and try
to pair with the first one it discovers using the provided setup code.

```
./BUILD_PATH/chip-tool pairing onnetwork-long <node_id> <pin_code> <disciminator>

```
where:
*<node_id>* is the user-defined ID of the node being commissioned,
*<pin_code>* and <disciminator> are device specific keys determined in step 4

The command below will discover devices based on the given QR code
(which devices log when they start up) and try to pair with the first one it discovers.

```
./BUILD_PATH/chip-tool qrcode <node_id> MT:<qrcode>

```
where:
*<node_id>* is the user-defined ID of the node being commissioned,
*<qrcode>* is the QR code

#### Forget the currently-commissioned device

```
./BUILD_PATH/chip-tool pairing unpair <node_id>

```
where:
*<node_id>* is the user-defined ID of the node which is going to be forgot by the `chip-tool`

#### Note
`chip-tool` currently only supports commissioning and remembering one device at a time.
The configuration state is cached in `/tmp/chip_tool_config.ini`; deleting this and other .ini files
in `/tmp` can sometimes resolve issues related to stale configuration.

### Step 6: Control application ZCL clusters.

For the lighting-app example, execute the following command to toggle the LED
state:

```
./BUILD_PATH/chip-tool onoff toggle <node_id> <endpoint_id>
```
where:
*<node_id>* is the user-defined ID of the commissioned node
*<endpoint_id>* is the ID of the endpoint with OnOff cluster implemented

To change the brightness of the LED, use the following command,
with the *<level>* equal to value between 0 and 255.

```
./BUILD_PATH/chip-tool levelcontrol move-to-level <level> <transiton_time> <option_mask> <option_override> <node_id> <endpoint_id>
```
where:
*<level>* is the brightness level encoded between 0 and 255
*<transiton_time>* is the transition time
*<option_mask>* is the option mask
*<option_override>* is the option override
*<node_id>* is the user-defined ID of the commissioned node
*<endpoint_id>* is the ID of the endpoint with LevelControl cluster implemented

In case of doubts regarding any of the options, please refer to the Matter specification.

### Step 7: Read basic information out of the accessory.

Every Matter accessory device supports a Basic Cluster, which maintains
collection of attributes that a controller can obtain from a device, such as the
vendor name, the product name, or software version. Use `read` command on `basic`
cluster to read those values from the device:

```
./BUILD_PATH/chip-tool basic read vendor-name <node_id> <endpoint_id>
./BUILD_PATH/chip-tool basic read product-name <node_id> <endpoint_id>
./BUILD_PATH/chip-tool basic read software-version <node_id> <endpoint_id>
```
where:
*<node_id>* is the user-defined ID of the commissioned node
*<endpoint_id>* is the ID of the endpoint with Basic cluster implemented

Use the following command to list all available commands for Basic cluster:

```
./BUILD_PATH/chip-tool basic
```

<hr>
