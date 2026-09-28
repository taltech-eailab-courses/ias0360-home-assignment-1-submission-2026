from operator import index
from turtle import *
import argparse

WIDTH = 280
HEIGHT = 240


def create_turtle_screen():
    screen = Screen()
    screen.setup(WIDTH*4, HEIGHT*4)
    screen.tracer(0)

    turtle = Turtle()
    turtle.color("black")
    turtle.hideturtle()
    turtle.speed(0)
    turtle.penup()

    return screen, turtle

def draw_image(turtle, image_path, index):
    with open(image_path, "r") as file_bin:
            rows = [line.rstrip("\r\n") for line in file_bin]
    
    if index == 0:

        point1 = (-(WIDTH*1.5) - 20, HEIGHT*1.5 + 20)
        point2 = (-(WIDTH/2)-20, HEIGHT*1.5 + 20)
        point3 = (-(WIDTH/2)-20, HEIGHT/2 + 20)
        point4 = (-(WIDTH*1.5) - 20, HEIGHT/2 + 20)

        turtle.goto(point1)
        turtle.pendown()
        turtle.goto(point2)
        turtle.goto(point3)
        turtle.goto(point4)
        turtle.goto(point1)
        turtle.penup()

        for j in range(0, HEIGHT):
            for i in range(0, WIDTH):
                turtle.goto(point1[0] + i, point1[1] - j)
                if rows[j][i] == "1":
                    turtle.dot(1)

    elif index == 1:

        point1 = (-WIDTH/2, HEIGHT*1.5 + 20)
        point2 = (WIDTH/2, HEIGHT*1.5 + 20)
        point3 = (WIDTH/2, HEIGHT/2 + 20)
        point4 = (-WIDTH/2, HEIGHT/2 + 20)

        turtle.goto(point1)
        turtle.pendown()
        turtle.goto(point2)
        turtle.goto(point3)
        turtle.goto(point4)
        turtle.goto(point1)
        turtle.penup()

        for j in range(0, HEIGHT):
            for i in range(0, WIDTH):
                turtle.goto(point1[0] + i, point1[1] - j)
                if rows[j][i] == "1":
                    turtle.dot(1)

    elif index == 2:

        point1 = ((WIDTH/2) + 20, HEIGHT*1.5 + 20)
        point2 = ((WIDTH*1.5) + 20, HEIGHT*1.5 + 20)
        point3 = ((WIDTH*1.5) + 20, HEIGHT/2 + 20)
        point4 = ((WIDTH/2) + 20, HEIGHT/2 + 20)

        turtle.goto(point1)
        turtle.pendown()
        turtle.goto(point2)
        turtle.goto(point3)
        turtle.goto(point4)
        turtle.goto(point1)
        turtle.penup()

        for j in range(0, HEIGHT):
            for i in range(0, WIDTH):
                turtle.goto(point1[0] + i, point1[1] - j)
                if rows[j][i] == "1":
                    turtle.dot(1)

    elif index == 3:

        point1 = (-(WIDTH*1.5) - 20, HEIGHT/2)
        point2 = (-(WIDTH/2) - 20, HEIGHT/2)
        point3 = (-(WIDTH/2) - 20, -HEIGHT/2)
        point4 = (-(WIDTH*1.5) - 20, -HEIGHT/2)
    
        turtle.goto(point1)
        turtle.pendown()
        turtle.goto(point2)
        turtle.goto(point3)
        turtle.goto(point4)
        turtle.goto(point1)
        turtle.penup()

        for j in range(0, HEIGHT):
            for i in range(0, WIDTH):
                turtle.goto(point1[0] + i, point1[1] - j)
                if rows[j][i] == "1":
                    turtle.dot(1)

    elif index == 4:

        point1 = (-WIDTH/2, HEIGHT/2)
        point2 = (WIDTH/2, HEIGHT/2)
        point3 = (WIDTH/2, -HEIGHT/2)
        point4 = (-WIDTH/2, -HEIGHT/2)
    
        turtle.goto(point1)
        turtle.pendown()
        turtle.goto(point2)
        turtle.goto(point3)
        turtle.goto(point4)
        turtle.goto(point1)
        turtle.penup()

        for j in range(0, HEIGHT):
            for i in range(0, WIDTH):
                turtle.goto(point1[0] + i, point1[1] - j)
                if rows[j][i] == "1":
                    turtle.dot(1)

    elif index == 5:

        point1 = ((WIDTH/2) + 20, HEIGHT/2)
        point2 = ((WIDTH*1.5) + 20, HEIGHT/2)
        point3 = ((WIDTH*1.5) + 20, -HEIGHT/2)
        point4 = ((WIDTH/2) + 20, -HEIGHT/2)
    
        turtle.goto(point1)
        turtle.pendown()
        turtle.goto(point2)
        turtle.goto(point3)
        turtle.goto(point4)
        turtle.goto(point1)
        turtle.penup()

        for j in range(0, HEIGHT):
            for i in range(0, WIDTH):
                turtle.goto(point1[0] + i, point1[1] - j)
                if rows[j][i] == "1":
                    turtle.dot(1)

    elif index == 6:

        point1 = (-(WIDTH*1.5) - 20, -HEIGHT/2 - 20)
        point2 = (-(WIDTH/2)-20, -HEIGHT/2 - 20)
        point3 = (-(WIDTH/2)-20, -HEIGHT*1.5 - 20)
        point4 = (-(WIDTH*1.5) - 20, -HEIGHT*1.5 -20)
    
        turtle.goto(point1)
        turtle.pendown()
        turtle.goto(point2)
        turtle.goto(point3)
        turtle.goto(point4)
        turtle.goto(point1)
        turtle.penup()

        for j in range(0, HEIGHT):
            for i in range(0, WIDTH):
                turtle.goto(point1[0] + i, point1[1] - j)
                if rows[j][i] == "1":
                    turtle.dot(1)

    elif index == 7:


        point1 = (-WIDTH/2, -HEIGHT/2 - 20)
        point2 = (WIDTH/2, -HEIGHT/2 - 20)
        point3 = (WIDTH/2, -HEIGHT*1.5 -20)
        point4 = (-WIDTH/2, -HEIGHT*1.5 -20)
    
        turtle.goto(point1)
        turtle.pendown()
        turtle.goto(point2)
        turtle.goto(point3)
        turtle.goto(point4)
        turtle.goto(point1)
        turtle.penup()

        for j in range(0, HEIGHT):
            for i in range(0, WIDTH):
                turtle.goto(point1[0] + i, point1[1] - j)
                if rows[j][i] == "1":
                    turtle.dot(1)

    elif index == 8:

        point1 = ((WIDTH/2) + 20, -HEIGHT/2 - 20)
        point2 = ((WIDTH*1.5) + 20, -HEIGHT/2 - 20)
        point3 = ((WIDTH*1.5) + 20, -HEIGHT*1.5 - 20)
        point4 = ((WIDTH/2) + 20, -HEIGHT*1.5 - 20)

        turtle.goto(point1)
        turtle.pendown()
        turtle.goto(point2)
        turtle.goto(point3)
        turtle.goto(point4)
        turtle.goto(point1)
        turtle.penup()

        for j in range(0, HEIGHT):
            for i in range(0, WIDTH):
                turtle.goto(point1[0] + i, point1[1] - j)
                if rows[j][i] == "1":
                    turtle.dot(1)



def main():
    parser = argparse.ArgumentParser(description="Processing arguments...")
    parser.add_argument("-v", "--version", help="Version of the preprocessing", default=000)
    args = parser.parse_args()

    screen, turtle = create_turtle_screen()

    image = [f"captures/{args.version}_original.txt",
             f"captures/{args.version}_original.txt", 
             f"captures/{args.version}_original.txt", 
             f"captures/{args.version}_low_pass.txt", 
             f"captures/{args.version}_high_pass.txt", 
             f"captures/{args.version}_low_pass_high_pass.txt",
             f"captures/{args.version}_downsampled_2.txt", 
             f"captures/{args.version}_downsampled_4.txt",
             f"captures/{args.version}_downsampled_8.txt"]
    for index in range(0, 9):
        print(f"Drawing {image}...")
        draw_image(turtle, image[index], index)

    screen.update()

    print("Drawing complete. Close the window to exit.")
    screen.mainloop()
            





if __name__ == "__main__":
    main()