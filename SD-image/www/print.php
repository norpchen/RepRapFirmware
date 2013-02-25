<!DOCTYPE HTML>
<head>
<style type="text/css">td { text-align: center; } </style>
</head>

<html>

<h2>RepRap: 
<?php print(getMyName()); ?>
<?php if(gotPassword()) echo '&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<a href="http://reprappro.com" target="_blank"><img src="logo.png" alt="RepRapPro logo"></a>'; ?>
</h2><br><br>
<?php if(printLinkTable()) echo '<table><tr>
      <td>&nbsp;&nbsp;&nbsp;<a href="control.php">Control</a>&nbsp;&nbsp;&nbsp;</td>

      <td>&nbsp;&nbsp;&nbsp;<a href="print.php">Print</a>&nbsp;&nbsp;&nbsp;</td>
 
    <td>&nbsp;&nbsp;&nbsp;<a href="http://reprap.org/wiki/RepRapPro_RepRap_Firmware" target="_blank">Help</a>&nbsp;&nbsp;&nbsp;</td>
 

      <td>&nbsp;&nbsp;&nbsp;<a href="settings.php">Settings</a>&nbsp;&nbsp;&nbsp;</td>
 
    <td>&nbsp;&nbsp;&nbsp;<a href="logout.php">Logout</a>&nbsp;&nbsp;&nbsp;</td>
      
    </tr></table>
  <br><br>'; ?>

<form action="print.php?upload=myFile"
enctype="multipart/form-data" method="post">
<p>
Upload a G Code file:<br>
<input type="file" name="datafile" size="40">
</p>
<div>
<input type="submit" value="Send">
</div>
</form>

<br><br>Click a file to print it:
<br><br>

<?php print(printGCodeTable()); ?>

<br><br>
<button type="button" onclick="return pausePrint()">Pause the print</button>
<br><br>
<hr>
<br>
<button type="button" onclick="return deleteFile()">Delete a file</button>



<script language="javascript" type="text/javascript">
   
function printFile(filetoprint){ window.location.href = "print.php?gcode=M23%20" + filetoprint + "%0AM24";}

function pausePrint(){ window.location.href = "print.php?gcode=M25";}

function deleteFile(){ window.location.href = "delete.php";}


</script> 

</html>
