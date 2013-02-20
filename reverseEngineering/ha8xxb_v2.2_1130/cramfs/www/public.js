function js_getReturn()
{
	alert(XMLData1.xml);
//var XMLData1=" ";
	var node = XMLData1.selectSingleNode('Root/Return').text;
//alert(node);
	if(node==0)
	{
		//alert("Sucess !");
	}
	else if(node==1)
	{   
	alert(document.all.set_error.innerText);
	}
	else if(node==2)
	{
		alert(document.all.get_fail.innerText);
	}
	else if(node==-2)
	{   
		alert(document.all.user_cut.innerText);
		top.close();
	}
	else if(node==-3)
	{   
		alert(document.all.not_permit.innerText);
		top.close();
	}
	else if(node==5)
	{
		alert(document.all.upgradesuccess.innerText);
	}
}

function js_setReturn()
{
//alert("111111js_setReturn11111");
//alert(XMLData2.xml);
	var node = XMLData2.selectSingleNode('Root/Return').text;
//alert("node="+node);
	if(node=="0")
	{    
	    /*if(lan=="language/english.xml")
		alert("Sucess !");
		else
		alert("成功");*/
		alert(document.all.sucess.innerText);
	}
	else if(node=="1")
	{   alert(document.all.set_error.innerText);
	}
	else if(node=="2")
	{   
	alert(document.all.get_fail.innerText);
	}
	else if(node=="3")
	{   
	alert(document.all.nopurview.innerText);
	}
	else if(node=="4")
	{   
	alert(document.all.userfull.innerText);
	}
	
}
function js_setReturn_1()
{
//alert("111111js_setReturn11111");
//alert(XMLData2.xml);
	var node = XMLData4.selectSingleNode('Root/Return').text;
//alert("node="+node);
	if(node=="0")
	{    
	    /*if(lan=="language/english.xml")
		alert("Sucess !");
		else
		alert("成功");*/
		alert(document.all.sucess.innerText);
	}
	else if(node=="1")
	{   alert(document.all.set_error.innerText);
	}
	else if(node=="2")
	{   
	alert(document.all.get_fail.innerText);
	}
}
