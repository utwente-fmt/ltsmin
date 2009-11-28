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

-- new functions
--
-- union two trees
union:: Set -> Set -> Set   --- <union(s,t)> = <s> union <t>

union Atom _ = Atom
union Empty s = s
union s Empty = s
union (Cons dstdown dstleft dstright) (Cons srcdown srcleft srcright) = Cons (union dstdown srcdown) (union dstleft srcleft) (union dstright srcright)

mcons Empty Empty Empty = Empty
mcons x y z = Cons x y z

-- subtract two trees
minus:: Set -> Set -> Set  --- <minus(s,t)> = <s> \ <t>
minus s Empty = s
minus Empty s = Empty
minus _ Atom = Empty
minus (Cons dstdown dstleft dstright) (Cons srcdown srcleft srcright) = mcons (minus dstdown srcdown) (minus dstleft srcleft) (minus dstright srcright)

-- zip: zip(s,t) = (s union t, t \ s)
tzip:: (Set, Set) -> (Set, Set)

tzip (Atom, _)  = (Atom, Empty)
tzip (s, Empty) = (s, Empty)
tzip (Empty, s) = (s, s)
tzip (Cons x s1 s2, Cons y t1 t2) = (Cons u u1 v1, mcons v u2 v2)
	where
		(u,v) =   tzip (x,y);
		(u1,u2) = tzip (s1,t1);
		(v1,v2) = tzip (s2,t2);

zipcheck:: (Set, Set) -> (Set, Set)
zipcheck (a, b) = (union a b, minus b a)

-- project
-- project: Set[n],Nat,Natlist[k] -> Set[k]
-- precondition project(s,i,j1..jk) :
--   i <= j1 < j2 < .. < jk <= i+n
--  (i is the offset of the first level in s, j1..jk are the levels to project on)

project:: Set -> Int -> [Int] -> Set

project Empty _ _ = Empty
project _ _ [] = Atom
project (Cons x s t) i (j:l) | i == j = Cons (project x (i+1) (l)) (project s i (j:l)) (project t i (j:l))
project (Cons x s t) i (j:l) | i < j  = union (project x (i+1) (j:l)) (union (project s i (j:l)) (project t i (j:l)) )

-- example
example:: Set -> Int -> Int -> [[Int]]

example Empty _ _ = []
example Atom _ _ = [[]]
example (Cons down Empty Empty) shift cur = map ((shift+cur):) (example down 1 0)
example (Cons down Empty right) shift cur = example right (shift*2) (shift+cur)
example (Cons down left right) shift cur = example left (shift*2) (cur)


-- rel add
rel_create [] [] = []
rel_create (src:from) (dst:to) = [src,dst] ++ (rel_create from to)


next Empty _ _ _ = Empty -- nothing left in src set
next _ Empty _ _ = Empty -- nothing left in relation
next src rel i [] = src
next src rel i (j:k) | i == j = apply src rel i (j:k)
next src rel i (j:k) | i < j  = copy  src rel i (j:k)

-- copy src_set relation offset proj -> copies this layer
copy Empty _ _ _ = Empty
-- copy Atom _ _ _ = Atom -- don't need this, all sets end on Cons Atom Empty Empty, 
--                        -- because Atom in left/right = empty set and there are no relations over emtpy sets
copy (Cons down left right) rel i (j:k) = mcons (next down rel (i+1) (j:k)) (copy left rel i (j:k)) (copy right rel i (j:k)) 

-- in apply, when we go down we need to transfer the part of the relation, and then continue with next
apply Empty _ _ _ = Empty
apply _ Empty _ _ = Empty -- not matched
apply (Cons x y z) (Cons u v w) i proj = union (trans x u i proj) (union (apply y v i proj) (apply z w i proj))

trans src Atom i (j:k) = Atom
trans src Empty i (j:k) = Empty
trans src (Cons down left right) i (j:k) = mcons (next src down (i+1) k) (trans src left i (j:k)) (trans src right i (j:k))

-- ==================================================
pcopy Empty _ _ _ = Empty
pcopy (Cons down left right) rel i (j:k) = mcons (prev down rel (i+1) (j:k)) (pcopy left rel i (j:k)) (pcopy right rel i (j:k)) 

prev Empty _ _ _ = Empty
prev _ Empty _ _ = Empty
prev src rel i [] = src
prev src rel i (j:k) | i == j = merge src rel i (j:k)
prev src rel i (j:k) | i <  j = pcopy src rel i (j:k)

-- merge src Atom _ _ = Atom -- shouldn't happen -> should be matched first
merge src Empty _ _= Empty
merge src (Cons u v w) i proj = mcons (match src u i proj) (merge src v i proj) (merge src w i proj)

match Empty _ _ _ = Empty
match _ Empty _ _ = Empty
match (Cons x y z) (Cons u v w) i (j:k) = union (prev x u (i+1) k) (union (match y v i (j:k)) (match z w i (j:k)))

-- set2list (prev (next (singleton [7,7,7,8]) (singleton [8,3]) 0 [3]) (singleton [6,3]) 0 [3]) 1 0
-- [[7,7,7,6]]
