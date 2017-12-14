package com.revature.as1.adv;

import java.lang.reflect.Field;

public class StringMethods {

	public static String replaceChar(String input, char target, char replace) {
		StringBuilder sb = new StringBuilder(input);

		int index = sb.indexOf(String.valueOf(target));
		if (index != -1) {
			if (index != 0) {
				return sb.substring(0, index) + replace + replaceChar(sb.substring(index + 1), target, replace);
			} else {
				return replace + replaceChar(sb.substring(index + 1), target, replace);
			}
		} else {
			return sb.toString();
		}
	}
	
	public static String reverseString(String in) throws NoSuchFieldException, SecurityException, IllegalArgumentException, IllegalAccessException {
		char[] chars = null;
		//try {
			Field value = String.class.getDeclaredField("value");
			value.setAccessible(true);
			
			chars = (char[])value.get(in);
			
			for(int i = 0; i < chars.length/2; i++) {
				chars[i] ^= chars[chars.length-1-i];
				chars[chars.length-1-i] ^= chars[i];
				chars[i] ^= chars[chars.length-1-i];
			}
	//	}catch(Exception e) {
		//	System.out.println(e.getMessage());
		//}
		
		return new String(chars);
	}

}
