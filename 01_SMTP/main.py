# File: main.py
# Author: Jack McShane (jamcshan)
# Created: 09/06/2021

# Description: This file defines an EmailBot class which iteracts with thet netcat
# interface provided in src/netcat.py to provide a basic email client
# functionality that allows the user to send email within the IU network.


from src.netcat import Netcat
from collections import namedtuple

SMTP_SERVER = 'mail-relay.iu.edu'
PORT_NUM = '25'



# EmailBot

# Defines a basic email client functionality taht allows the user to send emails
# within the IU network.
class EmailBot:
    # __init__(self)

    # Defines an 'Email' object for later use in building an email.
    # Defines the appropriate strings for passing SMTP commands to the connected
    # SMTP server.
    def __init__(self):
        self.Email = namedtuple('Email', 'sender recip subj content')
        self.SMTP_CMDS = namedtuple('SMTP_CMDS', 'helo mail rcpt data quit')
        self.smtp_cmds = self.SMTP_CMDS(
            'HELO silo.sice.indiana.edu\r\n',
            'MAIL From:<{}>\r\n',
            'RCPT To:<{}>\r\n',
            'DATA\r\n',
            'QUIT\r\n'
        )


    # build_from_input(self)
    # returns: namedtuple object 'Email'

    # Retrieves from the user the information that is necessary to build an email
    # that can be sent through the SMTP protocol.
    def build_from_input(self):
        print()
        sender = input("Sender\'s email: ")
        recip = input('Recipient\'s email: ')
        subj = input('Subject: ')
        cfile = input('File containing email contents: ')
        print()

        with open(cfile, 'r') as f:
            content = [line.rstrip('\n') for line in f.readlines()]

        self.email = self.Email(sender, recip, subj, content)


    # send_email(host=SMTP_SERVER, port=PORT_NUM)
    # host: a string that is either an IP address or a hostname that can be
    # resolved to an IP address
    # port: the port to which the connection with the server should be made

    # Sends an email using the interface provided by src/netcat.py and the
    # SMTP commands defined in the __init__ method for the EmailBot
    def send_email(self, host=SMTP_SERVER, port=PORT_NUM):
        # write_data()
        #
        # Write the DATA portion of the email to the SMTP server. This is the
        # only portion that cannot be done with a single line and therefore has
        # its own method.
        def write_data():
            self.nc.write(self.smtp_cmds.data)
            self.nc.write('Subject: {}\r\n'.format(self.email.subj))# write subject line
            for line in self.email.content: # write each line to email body
                self.nc.write('{}\r\n'.format(line))
            self.nc.write('.\r\n') # end email body

        self.nc = Netcat()
        self.nc.connect(host, port)
        self.nc.write(self.smtp_cmds.helo)
        self.nc.write(self.smtp_cmds.mail.format(self.email.sender))
        self.nc.write(self.smtp_cmds.rcpt.format(self.email.recip))
        write_data()
        self.nc.write(self.smtp_cmds.quit) # closes smtp connection
        self.nc.write(self.smtp_cmds.quit) # exits netcat



if __name__ == "__main__":

    bot = EmailBot()

    bot.build_from_input()
    bot.send_email()
