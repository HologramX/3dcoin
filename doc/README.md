3DCoin Core 0.12.1
=====================

This is the official reference wallet for 3DCoin digital currency and comprises the backbone of the 3DCoin peer-to-peer network. You can [download 3DCoin Core](https://www.3dcoin.io/downloads/) or [build it yourself](#building) using the guides below.

Running
---------------------
The following are some helpful notes on how to run 3DCoin on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/bitcoin-qt` (GUI) or
- `bin/bitcoind` (headless)

### Windows

Unpack the files into a directory, and then run 3dcoin-qt.exe.

### OS X

Drag 3DCoin-Qt to your applications folder, and then run 3DCoin-Qt.

### Need Help?

* See the [3DCoin documentation](https://3DCoin.atlassian.net/wiki/)
for help and more information.
* Ask for help on [#3DCoin](http://webchat.freenode.net?channels=3DCoin) on Freenode. If you don't have an IRC client use [webchat here](http://webchat.freenode.net?channels=3DCoin).
* Ask for help on the [3DCoinTalk](https://3dctalk.org/) forums.

Building
---------------------
The following are developer notes on how to build 3DCoin Core on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [OS X Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [Gitian Building Guide](gitian-building.md)

Development
---------------------
The 3DCoin Core repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Multiwallet Qt Development](multiwallet-qt.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- Source Code Documentation ***TODO***
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [Unit Tests](unit-tests.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)

### Resources
* Discuss on the [3DCoinTalk](https://3dcointalk.org/) forums, in the Development & Technical Discussion board.
* Discuss on [#BlockchainTechLLC](http://webchat.freenode.net/?channels=BlockchainTechLLC) on Freenode. If you don't have an IRC client use [webchat here](http://webchat.freenode.net/?channels=BlockchainTechLLC).

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)

License
---------------------
Distributed under the [MIT software license](http://www.opensource.org/licenses/mit-license.php).
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
