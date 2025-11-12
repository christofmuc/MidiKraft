# Introduction to MidiKraft

MidiKraft is C++ for writing software like editors and librarians to interface specific hardware MIDI devices.

MidiKraft are a set of base libraries to provide more helper classes to the awesome [JUCE](https://github.com/WeAreROLI/JUCE) framework useful when working with real life hardware MIDI synthesizers, and also to provide implementations for the individual synthesizers to be used in other programs.

 This is the current list of MidiKraft libraries available:

  * [MidiKraft-base](https://github.com/christofmuc/MidiKraft/tree/main/base): Base library used by all others providing some common definitions
  * [MidiKraft-librarian](https://github.com/christofmuc/MidiKraft/tree/main/librarian): Implementation of various different handshake and communication methods to retrieve and send data between the host computer and a MIDI device
  * [MidiKraft-database](https://github.com/christofmuc/MidiKraft/tree/main/database): Code to store MIDI data in a SQlite or AWS DynamoDB database

## MidiKraft Synthesizer implementations

We are working on a range of hardware synthesizer and other MIDI device implementations, to provide source code to utilize these devices to their fullest potential. The first ones that have been put into OpenSource are

  * [Behringer BCR 2000 MIDI Controller](https://github.com/christofmuc/MidiKraft-BCR2000)
  * [Behringer RD-8 Drum Machine](https://github.com/christofmuc/MidiKraft-behringer-rd8)
  * [Sequential (Dave Smith Instruments) Prophet Rev2](https://github.com/christofmuc/MidiKraft-sequential-rev2)
  
## MidiKraft derived software

The set of MidiKraft libraries is used in a range of software, including the following freely available software tools:

  * [JammerNetz](https://github.com/christofmuc/JammerNetz) online audio jamming sessions
  * The [KnobKraft Orm](https://github.com/christofmuc/KnobKraft-orm) Sysex Librarian
  * [PyTschirper](https://github.com/christofmuc/PyTschirper) python programming editor and IDE for MidiKraft synthesizers
  * [BCR2000_Master](https://github.com/christofmuc/BCR2000_Master) editor for the Behringer BCR2000 BCL language

## Usage

This MidiKraft repository is meant to be included as a git submodule in a main project, see the derived software repositories listed above for examples how this is used.

For what is in this library, please use the header files to see the utility functions, I am busy with my main projects and can't add more documentation right now.

## Licensing

As some substantial work has gone into the development of this and related software, I decided to offer a dual license - AGPL, see the LICENSE.md file for the details, for everybody interested in how this works and willing to spend some time her- or himself on this, and a commercial MIT license available from me on request. Thus I can help the OpenSource community without blocking possible commercial applications.

## Contributing

All pull requests and issues welcome, I will try to get back to you as soon as I can. Due to the dual licensing please be aware that I will need to request transfer of copyright on accepting a PR. 

## About the author

Christof is a lifelong software developer having worked in various industries, and can't stop his programming hobby anyway. 
