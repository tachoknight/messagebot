# `messagebot`
An IRC bot for sending public and private messages to users, written in C++ and using [SQLite](https://www.sqlite.org) and [libircclient](http://www.ulduzsoft.com/libircclient/). 

## Usage
### Public Messages
To send a message in the IRC room with messagebot running, type:
`!msg <nick> <message>`

This will send `<nick>` a *public* message, in the room running the bot. 
### Private Messages
To send a private message to a user, send a private message to messagebot using the same parameters as a public message. This will send `<nick>` a *private* message, whenever he or she logs on or changes to `<nick>` in the room running the bot (the room will not see the message).
