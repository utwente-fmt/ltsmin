-- this is meant as specification/documentation of vector_set_tree.c

module Vector where

data Set = Atom | Empty | Cons Set Set Set deriving (Show)

down (Cons s _ _) = s
left (Cons _ s _) = s
right (Cons _ _ s)= s

singleton []                = Atom
singleton (1:rest)          = Cons (singleton rest) Empty Empty
singleton (n:rest) | odd n  = Cons Empty Empty (singleton (n `div` 2 : rest))
singleton (n:rest) | even n = Cons Empty (singleton (n `div` 2 : rest)) Empty

add Empty l = singleton l
add Atom l = Atom
add (Cons down left right) (1:rest) = Cons (add down rest) left right
add (Cons down left right) (n:rest) | odd n  = Cons down left (add right (n `div` 2 : rest))
add (Cons down left right) (n:rest) | even n = Cons down (add left (n `div` 2 : rest)) right

member Empty l = False
member Atom [] = True
member (Cons down left right) (1:rest) = member down rest
member (Cons down left right) (n:rest) | odd n = member right (n `div` 2 : rest)
member (Cons down left right) (n:rest) | even n = member left (n `div` 2 : rest)

set2list:: Set -> Int -> Int -> [[Int]]

set2list Empty _ _ = []
set2list Atom _ _ = [[]]
set2list (Cons down left right) shift cur = map ((shift+cur):) (set2list down 1 0) ++
  set2list left (shift*2) cur ++ set2list right (shift*2) (shift+cur)

-- > set2list (add (add (singleton [5..14]) [1..10]) [3,5..22]) 1 0
-- [[1,2,3,4,5,6,7,8,9,10],[3,5,7,9,11,13,15,17,19,21],[5,6,7,8,9,10,11,12,13,14]]
