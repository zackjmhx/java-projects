package com.revature.as1.exception;

public class ExceptionDriver {
	
	public static void main(String[] args) throws RevatureException {
		try {
			System.out.println("We're in the try block. Get ready for things to go wrong.");
			throw(new RevatureException());
		}catch(Exception e) {
			System.out.println("We made it into the catch block! Error: " + e.getMessage());
		}finally {
			System.out.println("That could have gone worse!");
		}
		
		exceptionalMethod();
		
	}
	
	public static void exceptionalMethod() throws RevatureException{
			System.out.println("Ducking this bizzare exception!");
			System.exit(0); //Exception never actually thrown, because this method is a trap. Pedagogical examples get weird sometimes
			throw (new RevatureException());
	}

}
