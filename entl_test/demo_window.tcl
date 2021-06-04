#!/usr/local/bin/wish
#---------------------------------------------------------------------------------------------
 #  Copyright © 2016-present Earth Computing Corporation. All rights reserved.
 #  Licensed under the MIT License. See LICENSE.txt in the project root for license information.
#---------------------------------------------------------------------------------------------

# first arg is Socket port number, 2nd arg is the device name
set DeviceName [lindex $argv 0]
set SocketPort [lindex $argv 1]

set Line ""
set ReadState "Idle"

# Color of the windows 
set Cscrollb "SkyBlue"
set Cbuttonf "black"
set Cbuttonb "SkyBlue"
set Cquitbuttonb "red"
set Clabelf "black"
set Clabelb "SkyBlue"
set Centryf "black"
set Centryb "white"
set Ctextf "black"
set Ctextb "white"

set Cgreen "green"
set Cred "red"
set Cyellow "yellow"


### Window Structure ###
# f_info : show device name
# f_sim_cntl : < run >  <quit>
# f_entl_state : show state and value
# f_link_state : show link state
# f_ait_send : text  <send>
# f_ait_state :  received AIT token
#

font create AppHighlightFont -family Helvetica -size 32 -weight bold
font create AppLowlightFont -family Helvetica -size 24 -weight bold

frame .f_info
frame .f_sim_cntl
frame .f_entl_state
frame .f_ait_send
frame .f_ait_state

label .f_info.l  -text "Device:" -font AppHighlightFont  -foreground $Clabelf -background $Clabelb  -width 10
label .f_info.name  -text $DeviceName -font AppHighlightFont -foreground $Clabelf -background $Ctextb -width 10 
label .f_info.link  -text " Link:" -font AppHighlightFont  -foreground $Clabelf -background $Clabelb -width 10  
label .f_info.link_state  -text "Down"  -font AppHighlightFont -foreground $Clabelf -background $Cred -width 10  

button .f_sim_cntl.bt1 -text "Start" -font AppLowlightFont -width 20 \
  -foreground $Cbuttonf -background $Cbuttonb \
  -command { exec_start_command }

button .f_sim_cntl.bt2 -text "Quit" -font AppLowlightFont -width 20 \
  -foreground $Cbuttonf -background $Cquitbuttonb \
  -command { exec_quit_command }

label .f_entl_state.st_label -text "Idle"  -font AppHighlightFont  -foreground $Clabelf -background $Cred -width 20  
label .f_entl_state.st_value -text "0"  -font AppHighlightFont -foreground $Clabelf -background $Centryb -width 20  


label .f_ait_send.ait -text "Send Msg via AIT:" -font AppLowlightFont -foreground $Clabelf -background $Clabelb -width 25
entry .f_ait_send.message -textvariable Command -width 40 -font AppLowlightFont \
  -foreground $Centryf -background $Centryb

label .f_ait_state.ait -text "Received Msg via AIT:" -font AppLowlightFont -foreground $Clabelf -background $Clabelb -width 25
label .f_ait_state.ait_get -text "-none-" -font AppLowlightFont  -foreground $Centryf -background $Centryb -width 40

##### packing frames
pack .f_info.l .f_info.name .f_info.link .f_info.link_state -side left
pack .f_sim_cntl.bt1 .f_sim_cntl.bt2 -side left
pack .f_entl_state.st_label .f_entl_state.st_value -side left
pack .f_ait_send.ait .f_ait_send.message -side left
pack .f_ait_state.ait .f_ait_state.ait_get -side left

##Top frame
pack .f_info  -expand 1 -fill both
pack .f_entl_state  -expand 1 -fill both
pack .f_ait_send  -expand 1 -fill both
pack .f_ait_state -expand 1 -fill both
pack .f_sim_cntl 


# Command field action
bind .f_ait_send.message <Key-Return> {
    global jdbSocket

    puts $jdbSocket "AIT $Command\n"
}

if { $SocketPort != "" } {
  #Open socket for communication
  puts stderr "Socport = $SocketPort"
  set jdbSocket [socket localhost $SocketPort]
  fconfigure $jdbSocket -buffering line
  fileevent $jdbSocket readable exec_read_loop
}

proc sleep {time} {
    after $time set end 1
    vwait end
}

proc exec_start_command { } {
  global jdbSocket
	puts $jdbSocket "start\n"
}

proc exec_quit_command { } {
  global jdbSocket
	puts $jdbSocket "quit\n"
  sleep 5
  exit 
}

proc show_link line {
	global Cred
	global Cyellow
	global Cgreen

	if { $line == "UP" } {
		.f_info.link_state configure -text $line -background $Cgreen
	} elseif { $line == "DOWN" } {
		.f_info.link_state configure -text $line -background $Cred
	} else {
		.f_info.link_state  configure -text $line -background $Cyellow
	}

}

proc show_state line {
	global Cred
	global Cyellow
	global Cgreen

	if { $line == "Idle" } {
		.f_entl_state.st_label configure -text $line -background $Cred
	} elseif { $line == "Hello" || $line == "Wait" } {
		.f_entl_state.st_label configure -text $line -background $Cyellow
	} else {
		.f_entl_state.st_label configure -text $line -background $Cgreen
	}
}

proc show_value line {
	.f_entl_state.st_value configure -text $line 
}

proc show_ait line {
	.f_ait_state.ait_get configure -text $line
}

proc exec_read_loop { } {
  global jdbSocket
  global Line
  global ReadState
  
  gets $jdbSocket Line
  if { $Line!="" } {
  		#puts stderr "got $Line on state $ReadState"  
  }
  if { $Line=="" } {
  } elseif { $ReadState=="Idle" } {
  	if { $Line == "#AIT" } {
  		set ReadState "AIT"
  		#puts stderr "got AIT"
  	} elseif { $Line == "#State" } {
  		set ReadState "State"
   		#puts stderr "got State"
 	} elseif { $Line == "#Value" } {
  		set ReadState "Value"
   		#puts stderr "got Value"
 	} elseif { $Line == "#Link" } {
  		set ReadState "Link"
   		#puts stderr "got Value"
 	}

  } elseif { $ReadState== "AIT" } {
  	#puts stderr "got AIT $Line"
  	show_ait $Line
  	set ReadState "Idle"
  } elseif { $ReadState== "State" } {
  	#puts stderr "got State $Line"
  	show_state $Line
  	set ReadState "Idle"
	#puts $jdbSocket "\n"
  } elseif { $ReadState== "Value" } {
  	#puts stderr "got Value $Line"
  	show_value $Line
  	set ReadState "Idle"
	#puts $jdbSocket "\n"
  } elseif { $ReadState== "Link" } {
  	#puts stderr "got Link $Line"
  	show_link $Line
  	set ReadState "Idle"
	#puts $jdbSocket "\n"
  } 
 
  #puts stderr "now $Line on state $ReadState"

}


#testing
#  show_value "5546543"
