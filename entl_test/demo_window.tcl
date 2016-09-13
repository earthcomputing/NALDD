#!/usr/local/bin/wish
#   Copyright (C) 2016 Earth Computing

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

frame .f_info
frame .f_sim_cntl
frame .f_entl_state
frame .f_ait_send
frame .f_ait_state

label .f_info.name  -text $DeviceName -foreground $Clabelf -background $Clabelb

button .f_sim_cntl.bt1 -text "Start" \
  -foreground $Cbuttonf -background $Cbuttonb \
  -command { exec_start_command }

button .f_sim_cntl.bt2 -text "Quit" \
  -foreground $Cbuttonf -background $Cquitbuttonb \
  -command { exec_quit_command }

label .f_entl_state.st_label -text "Idle" -foreground $Clabelf -background $Cred -width 30
label .f_entl_state.st_value -text "0" -foreground $Clabelf -background $Centryb -width 30


label .f_ait_send.ait -text "Send AIT:" -foreground $Clabelf -background $Clabelb
entry .f_ait_send.message -textvariable Command -width 50 \
  -foreground $Centryf -background $Centryb

label .f_ait_state.ait -text "Received AIT:" -foreground $Clabelf -background $Clabelb
label .f_ait_state.ait_get -text "-none-" -foreground $Centryf -background $Centryb

##### packing frames
pack .f_info.name 
pack .f_sim_cntl.bt1 .f_sim_cntl.bt2 -side left
pack .f_entl_state.st_label .f_entl_state.st_value -side left
pack .f_ait_send.ait .f_ait_send.message -side left
pack .f_ait_state.ait .f_ait_state.ait_get -side left

##Top frame
pack .f_info  -expand 1 -fill both
pack .f_sim_cntl
pack .f_entl_state
pack .f_ait_send
pack .f_ait_state -expand 1 -fill both


# Command field action
bind .f_ait_send.message <Key-Return> {
  puts "ATI $Command"
}

if { $SocketPort != "" } {
  #Open socket for communication
  puts stderr "Socport = $SocketPort"
  set jdbSocket [socket localhost $SocketPort]
  fconfigure $jdbSocket -buffering line
  fileevent $jdbSocket readable exec_read_loop
}



proc exec_start_command { } {
	puts "start"
}

proc exec_quit_command { } {
	puts "quit"
}

proc show_state line {
	global Cred
	global Cyellow
	global Cgreen

	if { $line == "Idle" } {
		.f_entl_state.st_label configure -text $line -background $Cred
	} elseif { $line == "Hello" } {
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
  
  gets $jdbSocket Line

  if { $Line=="" } {
  } elseif { $ReadState=="Idle" } {
  	if { $Line == "#AIT" } {
  		set $ReadState "AIT"
  	} elseif { $Line == "#State" } {
  		$ReadState = "State"
  	} elseif { $Line == "#Value" } {
  		$ReadState = "Value"
  	}
  } elseif { $ReadState== "AIT" } {
  	show_ait $Line
  } elseif { $ReadState== "State" } {
  	show_state $Line
  } elseif { $ReadState== "Value" } {
  	show_value $Line
  } 
  set ReadState "Idle"
}


#testing
#show_value "5546543"
